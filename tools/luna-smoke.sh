#!/bin/bash
# Drive the live pdm luna API on a target and check the daemon survives it.
#
# The unit tests cover what pdm computes; this covers what it does when a
# client talks nonsense at it. Every call that used to be able to take the
# daemon down went through a luna method: a malformed payload, a drive name
# that matches nothing, an unparseable number. After each round we check that
# physical-device-manager still holds the pid it started with - a new pid means
# it crashed and systemd picked it back up.
#
#   tools/luna-smoke.sh sargo
#   tools/luna-smoke.sh vbox
#
# Read-only and safe: format is deliberately never called, and every drive name
# used here is one no attached device can match.
#
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
# SPDX-License-Identifier: Apache-2.0

set -u
cd "$(dirname "$0")/.."
. tools/pdm-test-env.sh

TARGET="${1:-}"
[ -n "$TARGET" ] || pdm_die "usage: $0 <sargo|vbox>"

tgt_run() {
    case "$TARGET" in
        sargo) adb shell "$@" ;;
        vbox)  # shellcheck disable=SC2086
               ssh $PDM_SSH_OPTS -p "$PDM_VBOX_SSH_PORT" "$PDM_VBOX_SSH_HOST" "$@" ;;
        *)     pdm_die "unknown target '$TARGET'" ;;
    esac
}

SVC="luna://com.webos.service.pdm"
PASS=0; FAIL=0

pdm_pid() { tgt_run 'pidof physical-device-manager 2>/dev/null || echo none' | tr -d '\r' | awk '{print $1}'; }

# call <method> <payload> -> prints the reply
call() { tgt_run "luna-send -n 1 -t 5 $SVC/$1 '$2' 2>&1" | tr -d '\r'; }

check() {  # check <description> <expected-substring> <reply>
    if printf '%s' "$3" | grep -qF -- "$2"; then
        echo "  ok    $1"
        PASS=$((PASS+1))
    else
        echo "  FAIL  $1"
        echo "        expected to contain: $2"
        echo "        got: $(printf '%s' "$3" | head -c 300)"
        FAIL=$((FAIL+1))
    fi
}

alive() {  # alive <description> <pid-before>
    local now; now=$(pdm_pid)
    if [ "$now" = "$2" ] && [ "$now" != "none" ]; then
        echo "  ok    $1 (pid $now unchanged)"
        PASS=$((PASS+1))
    else
        echo "  FAIL  $1 - pid went $2 -> $now (the daemon restarted)"
        FAIL=$((FAIL+1))
    fi
}

pdm_info "target: $TARGET"
tgt_run 'true' >/dev/null 2>&1 || pdm_die "cannot reach $TARGET"

START_PID=$(pdm_pid)
[ "$START_PID" != "none" ] || pdm_die "physical-device-manager is not running on $TARGET"
pdm_info "physical-device-manager pid $START_PID"
echo

# --- the service answers ----------------------------------------------------
# Checked on the list the call is supposed to produce rather than on
# returnValue, because the three getAttached* methods report
#
#     payload.put("returnValue", subscribed);
#
# so a non-subscribing query that worked perfectly comes back
# "returnValue":false. See the note at the bottom of tools/README.md - it is a
# real bug, but changing it changes the API, so it is not fixed here and the
# smoke test does not pretend otherwise.
echo "service responds:"
check "getAttachedStorageDeviceList"    '"storageDeviceList"'    "$(call getAttachedStorageDeviceList '{"subscribe":false}')"
check "getAttachedNonStorageDeviceList" '"nonStorageDeviceList"' "$(call getAttachedNonStorageDeviceList '{"subscribe":false}')"
check "getAttachedDeviceStatus"         '"deviceStatusList"'     "$(call getAttachedDeviceStatus '{"subscribe":false}')"
echo

# --- schema validation rejects, it does not crash ---------------------------
echo "schema validation:"
check "getSpaceInfo without driveName"  '"returnValue":false' "$(call getSpaceInfo '{}')"
check "setVolumeLabel without label"    '"returnValue":false' "$(call setVolumeLabel '{"driveName":"sdz9"}')"
check "fsck with a numeric driveName"   '"returnValue":false' "$(call fsck '{"driveName":1234}')"
check "eject with a string deviceNum"   '"returnValue":false' "$(call eject '{"deviceNum":"not-a-number"}')"
alive "survived schema validation" "$START_PID"
echo

# --- drive names that match nothing ----------------------------------------
# These all reach the device lookup. None can match a real device, so none of
# them reaches a command line; what is being checked is that a miss is an error
# reply rather than an exception.
echo "unmatched drive names:"
for name in 'pdmtest-nonexistent' 'pdmtest; echo hi' 'pdmtest$(id)' 'pdmtest|id' '../../etc/passwd'; do
    reply=$(call getSpaceInfo "{\"driveName\":\"$name\"}")
    check "getSpaceInfo '$name' declines" '"returnValue":false' "$reply"
done
alive "survived unmatched drive names" "$START_PID"
echo

# --- payloads that are not what the schema promised -------------------------
echo "malformed payloads:"
LONG=$(head -c 4096 /dev/zero | tr '\0' 'A')
ROUNDS=0
for payload in \
    'not json at all' \
    '{' \
    '[]' \
    'null' \
    '{"subscribe":"yes"}' \
    '{"driveName":null}' \
    '{"driveName":{"nested":true}}' \
    "{\"driveName\":\"$LONG\"}" \
    ; do
    for method in getSpaceInfo getAttachedStorageDeviceList setVolumeLabel isWritableDrive; do
        call "$method" "$payload" >/dev/null 2>&1
    done
    ROUNDS=$((ROUNDS+1))
done
echo "  sent $ROUNDS malformed payloads to 4 methods each"
alive "survived malformed payloads" "$START_PID"
echo

# --- volume labels a shell would have loved ---------------------------------
# The drive names cannot match, so no relabel actually runs; this checks the
# request is refused cleanly rather than throwing on the way to the refusal.
# The label injection itself is covered by PdmUtilsRunCommand in the unit
# tests, which does not need a real disk to prove it.
echo "hostile volume labels:"
MARKER="/tmp/pdm-smoke-pwned-$$"
tgt_run "rm -f $MARKER"
for label in "x; touch $MARKER" "x\$(touch $MARKER)" "x && touch $MARKER"; do
    call setVolumeLabel "{\"driveName\":\"pdmtest-nonexistent\",\"volumeLabel\":\"$label\"}" >/dev/null 2>&1
done
marker_present=$(tgt_run "[ -e $MARKER ] && echo yes || echo no" | tr -d '\r')
check "no injected command ran" "no" "$marker_present"
alive "survived hostile labels" "$START_PID"
echo

# --- subscriptions ----------------------------------------------------------
echo "subscriptions:"
sub_reply=$(tgt_run "luna-send -i -t 3 $SVC/getAttachedStorageDeviceList '{\"subscribe\":true}' 2>&1 | head -n 1" | tr -d '\r')
# With subscribe:true, returnValue does report the subscription, so here it is
# meaningful and true.
check "subscribe is accepted" '"returnValue":true' "$sub_reply"
alive "survived subscribe" "$START_PID"
echo

echo "=================== pdm luna smoke ==================="
echo "target: $TARGET   passed: $PASS   failed: $FAIL"
echo "======================================================"
[ "$FAIL" -eq 0 ]
