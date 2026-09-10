#!/bin/bash
# Build the pdm unit tests, install them on a target and run them there.
#
# The tests are built by the cross toolchain and run on the real hardware, so
# armv7's 32-bit long and aarch64's alignment rules are exercised by the same
# binary the daemon is built from - not by a host build that happens to pass.
#
#   tools/run-tests.sh sargo      # Pixel 3a over adb (aarch64)
#   tools/run-tests.sh vbox       # LuneOS VirtualBox VM over ssh (x86-64)
#   tools/run-tests.sh sargo --gtest_filter='PdmUtilsRunCommand.*'
#
# Anything after the target is handed to the gtest binary.
#
# Only the -tests package is installed: gtest and -core are both linked
# statically into pdm_test, so it has no runtime dependency on either and the
# daemon already on the target is left running. Pass --with-daemon to install
# the freshly built com.webos.service.pdm too and restart it - do that before
# tools/luna-smoke.sh if you want the smoke test to exercise new code.
#
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
# SPDX-License-Identifier: Apache-2.0

set -u
cd "$(dirname "$0")/.."
. tools/pdm-test-env.sh

TARGET="${1:-}"
[ -n "$TARGET" ] || pdm_die "usage: $0 <sargo|vbox> [--with-daemon] [gtest args...]"
shift

WITH_DAEMON=0
if [ "${1:-}" = "--with-daemon" ]; then
    WITH_DAEMON=1
    shift
fi

case "$TARGET" in
    sargo) ARCH=aarch64 ;;
    vbox)  ARCH=x86_64 ;;
    *)     pdm_die "unknown target '$TARGET' (expected sargo or vbox)" ;;
esac
MACHINE=$(pdm_machine_for "$ARCH")

# --- target transport -------------------------------------------------------

tgt_probe() {
    case "$TARGET" in
        sargo) adb get-state >/dev/null 2>&1 ;;
        vbox)  # shellcheck disable=SC2086
               ssh $PDM_SSH_OPTS -p "$PDM_VBOX_SSH_PORT" "$PDM_VBOX_SSH_HOST" true >/dev/null 2>&1 ;;
    esac
}

tgt_run() {
    case "$TARGET" in
        sargo) adb shell "$@" ;;
        vbox)  # shellcheck disable=SC2086
               ssh $PDM_SSH_OPTS -p "$PDM_VBOX_SSH_PORT" "$PDM_VBOX_SSH_HOST" "$@" ;;
    esac
}

# adb shell reports the exit status of adb, not of the command it ran, so a
# failing test suite comes back as success. Have the target print its own
# status and read that instead. RC is set as a side effect.
RC=0
tgt_run_checked() {
    local marker='__PDM_RC__'
    local out
    out=$(tgt_run "$* ; echo ${marker}\$?" 2>&1 | tr -d '\r')
    RC=$(printf '%s' "$out" | sed -n "s/^${marker}//p" | tail -n 1)
    printf '%s\n' "$out" | grep -v "^${marker}"
    [ -n "$RC" ] || RC=1
    return 0
}

tgt_push() {
    case "$TARGET" in
        sargo) adb push "$1" "$2" >/dev/null ;;
        vbox)  # shellcheck disable=SC2086
               scp $PDM_SSH_OPTS -P "$PDM_VBOX_SSH_PORT" "$1" "$PDM_VBOX_SSH_HOST:$2" >/dev/null ;;
    esac
}

if ! tgt_probe; then
    case "$TARGET" in
        sargo) pdm_die "no device over adb (adb devices)" ;;
        vbox)  pdm_die "no ssh on port $PDM_VBOX_SSH_PORT - is $PDM_VBOX_VM running? (VBoxManage startvm \"$PDM_VBOX_VM\" --type headless)" ;;
    esac
fi

# --- build ------------------------------------------------------------------

CONF=$(mktemp /tmp/pdm-externalsrc-XXXXXX.conf)
trap 'rm -f "$CONF"' EXIT
pdm_write_externalsrc_conf "$CONF"

pdm_info "building $PDM_RECIPE for MACHINE=$MACHINE from $PDM_SRC_DIR"
BUILDLOG=$(mktemp /tmp/pdm-build-XXXXXX.log)
if ! ( cd "$PDM_BUILD_DIR" && MACHINE="$MACHINE" bash -c '
        set -e
        cd "$1"
        . ./setup-env >/dev/null 2>&1 || . ./oe-init-build-env . >/dev/null 2>&1
        exec bitbake -R "$2" "$3"
      ' _ "$PDM_BUILD_DIR" "$CONF" "$PDM_RECIPE" ) > "$BUILDLOG" 2>&1; then
    grep -E '^ERROR' "$BUILDLOG" | head -10
    pdm_die "build failed (full log: $BUILDLOG)"
fi

# --- locate and install the ipks -------------------------------------------

IPKDIR="$PDM_BUILD_DIR/tmp/deploy/ipk"
# OE stamps package files with SOURCE_DATE_EPOCH, so "newest by mtime" finds
# nothing. pdm_write_externalsrc_conf sets PV to 1.0.1-local, which only the
# harness builds carry, so match on that.
find_ipk() { find "$IPKDIR" -name "$1" 2>/dev/null | sort | tail -n 1; }

TESTS_IPK=$(find_ipk "${PDM_RECIPE}-tests_1.0.1-local-*.ipk")
MAIN_IPK=$(find_ipk "${PDM_RECIPE}_1.0.1-local-*.ipk")

[ -n "$TESTS_IPK" ] || pdm_die "no ${PDM_RECIPE}-tests ipk under $IPKDIR - is WEBOS_CONFIG_INSTALL_TESTS on in the recipe?"

# gtest is linked statically into pdm_test (readelf shows no libgtest), so
# there is nothing else to install.
INSTALL_LIST="$TESTS_IPK"
if [ "$WITH_DAEMON" = "1" ]; then
    [ -n "$MAIN_IPK" ] || pdm_die "--with-daemon asked for, but no ${PDM_RECIPE} ipk was found"
    INSTALL_LIST="$MAIN_IPK $INSTALL_LIST"
    pdm_info "--with-daemon: the running physical-device-manager will be replaced"
fi

pdm_info "installing on $TARGET: $(for i in $INSTALL_LIST; do basename "$i"; done | tr '\n' ' ')"
tgt_run 'rm -rf /tmp/pdm-tests && mkdir -p /tmp/pdm-tests'
for ipk in $INSTALL_LIST; do
    tgt_push "$ipk" "/tmp/pdm-tests/$(basename "$ipk")"
done
tgt_run_checked 'opkg install --force-reinstall --force-downgrade /tmp/pdm-tests/*.ipk 2>&1 | tail -n 20'
[ "$RC" = "0" ] || pdm_die "opkg install failed on $TARGET (exit $RC)"

if [ "$WITH_DAEMON" = "1" ]; then
    pdm_info "restarting physical-device-manager"
    tgt_run 'systemctl restart physical-device-manager 2>&1; sleep 2; systemctl is-active physical-device-manager'
fi

# --- run --------------------------------------------------------------------

# busybox head has no -1; -n 1 works on both busybox and coreutils.
TESTBIN=$(tgt_run 'find /usr/opt/webos/tests /usr/libexec/tests -name pdm_test -type f 2>/dev/null | head -n 1' | tr -d '\r')
case "$TESTBIN" in
    /*) ;;
    *)  pdm_die "pdm_test not found on $TARGET after install (got: ${TESTBIN:-nothing})" ;;
esac

pdm_info "running $TESTBIN on $TARGET ($ARCH)"
echo
tgt_run_checked "$TESTBIN $*"
rc=$RC

echo
if [ "$rc" -eq 0 ]; then
    pdm_info "$TARGET ($ARCH): PASSED"
else
    pdm_info "$TARGET ($ARCH): FAILED (exit $rc)"
fi
exit "$rc"
