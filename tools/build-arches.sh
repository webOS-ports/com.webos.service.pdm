#!/bin/bash
# Build com.webos.service.pdm for every architecture we ship and fail on any
# compiler warning.
#
# The three targets differ in ways that have bitten this code before: uint64_t
# is unsigned long on aarch64 and unsigned long long on armv7, size_t and long
# are 32-bit on armv7, and the aarch64 build carries -mbranch-protection. A
# clean build on one says little about the others.
#
# Builds this working tree, not the SRCREV the recipe pins, so uncommitted work
# is what gets checked. Nothing in the layer is modified.
#
#   tools/build-arches.sh                 # all three
#   tools/build-arches.sh armv7 aarch64   # a subset
#   PDM_ARCHES="x86_64" tools/build-arches.sh
#
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
# SPDX-License-Identifier: Apache-2.0

set -u
cd "$(dirname "$0")/.."
. tools/pdm-test-env.sh

[ $# -gt 0 ] && PDM_ARCHES="$*"

[ -d "$PDM_BUILD_DIR" ] || pdm_die "no OE build at $PDM_BUILD_DIR (set PDM_BUILD_DIR)"
[ -f "$PDM_BUILD_DIR/conf/local.conf" ] || pdm_die "$PDM_BUILD_DIR is not a bitbake build directory"

CONF=$(mktemp /tmp/pdm-externalsrc-XXXXXX.conf)
trap 'rm -f "$CONF"' EXIT
pdm_write_externalsrc_conf "$CONF"

LOGDIR="${PDM_LOG_DIR:-$(mktemp -d /tmp/pdm-build-logs-XXXXXX)}"
mkdir -p "$LOGDIR"
pdm_info "source:  $PDM_SRC_DIR"
pdm_info "build:   $PDM_BUILD_DIR"
pdm_info "logs:    $LOGDIR"

overall=0
summary=()

for arch in $PDM_ARCHES; do
    machine=$(pdm_machine_for "$arch") || { overall=1; continue; }
    log="$LOGDIR/$arch.log"

    pdm_info "[$arch] building for MACHINE=$machine"
    # A subshell so the oe-init-build-env each arch needs cannot leak sideways.
    (
        cd "$PDM_BUILD_DIR" || exit 1
        # shellcheck disable=SC1091
        MACHINE="$machine" bash -c '
            set -e
            cd "$1"
            . ./setup-env >/dev/null 2>&1 || . ./oe-init-build-env . >/dev/null 2>&1
            exec bitbake -R "$2" -c compile -f "$3"
        ' _ "$PDM_BUILD_DIR" "$CONF" "$PDM_RECIPE"
    ) > "$log" 2>&1
    rc=$?

    if [ $rc -ne 0 ]; then
        summary+=("$arch  BUILD FAILED  (see $log)")
        grep -E '^ERROR' "$log" | head -5
        overall=1
        continue
    fi

    # do_compile's own log is where the warnings are; the console log only
    # summarises. Find the workdir bitbake actually used.
    workdir=$(cd "$PDM_BUILD_DIR" && ls -dt tmp/work/*/"$PDM_RECIPE"/*/temp 2>/dev/null | head -1)
    complog="$PDM_BUILD_DIR/$workdir/log.do_compile"
    if [ ! -f "$complog" ]; then
        summary+=("$arch  built, but no log.do_compile found")
        overall=1
        continue
    fi
    cp "$complog" "$LOGDIR/$arch.do_compile.log"

    # Only warnings from our own sources; the sysroot's headers are not ours
    # to fix.
    warnings=$(grep 'warning:' "$complog" | grep -c "$PDM_RECIPE" || true)
    if [ "$warnings" -eq 0 ]; then
        summary+=("$arch  OK  (0 warnings)")
    else
        summary+=("$arch  $warnings WARNINGS  (see $LOGDIR/$arch.do_compile.log)")
        grep 'warning:' "$complog" | grep "$PDM_RECIPE" | sed 's|.*/git/||;s|.*/'"$PDM_RECIPE"'[^/]*/||' | sort -u | head -20
        overall=1
    fi
done

echo
echo "================ pdm multi-arch build ================"
printf '%s\n' "${summary[@]}"
echo "======================================================"
exit $overall
