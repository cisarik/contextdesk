#!/usr/bin/env bash
# Fast device-free checks for contextdeck-trial-cutoff.sh. Does not start the
# broker, open input devices, or touch production units.
set -euo pipefail

root="${1:-}"
if [[ -z "$root" ]]; then
    echo "usage: $0 SOURCE_DIR" >&2
    exit 2
fi

helper="${root}/packaging/systemd/contextdeck-trial-cutoff.sh"
unit_file="${root}/packaging/systemd/contextdeck-broker.service"
failures=0

fail() {
    printf 'FAIL %s\n' "$*" >&2
    failures=$((failures + 1))
}

bash -n "$helper"
if [[ ! -x "$helper" ]]; then
    chmod +x "$helper"
fi
if [[ ! -x "$helper" ]]; then
    fail "helper not executable"
fi

verify_out="$(mktemp)"
if ! systemd-analyze verify "$unit_file" >"$verify_out" 2>&1; then
    if ! grep -E 'Command line is not executable|is not executable|Failed to prepare filename' "$verify_out" >/dev/null; then
        cat "$verify_out" >&2
        fail "systemd-analyze verify production unit"
    fi
fi
rm -f "$verify_out"

if grep -E '^(RuntimeMaxSec|EXTEND_TIMEOUT)' "$unit_file"; then
    fail "forbidden lifetime extension in unit"
fi
grep -q '^Type=notify$' "$unit_file" || fail "Type=notify"
grep -q '^NotifyAccess=main$' "$unit_file" || fail "NotifyAccess=main"
grep -q '^WatchdogSec=2$' "$unit_file" || fail "WatchdogSec=2"
grep -q '^TimeoutStopSec=5$' "$unit_file" || fail "TimeoutStopSec=5"
grep -q '^TimeoutAbortSec=5$' "$unit_file" || fail "TimeoutAbortSec=5"
grep -q '^Restart=no$' "$unit_file" || fail "Restart=no"
grep -q '^LimitCORE=0$' "$unit_file" || fail "LimitCORE=0"
grep -q '^RuntimeDirectoryMode=0755$' "$unit_file" || fail "RuntimeDirectoryMode"
if grep -q '^\[Install\]' "$unit_file"; then
    fail "Install section must not exist"
fi

set +e
"$helper" >/dev/null 2>&1
rc=$?
set -e
if [[ $rc -ne 2 ]]; then
    fail "usage should exit 2"
fi

set +e
out="$("$helper" --timeout=19 arm no-such-unit 01234567-89ab-cdef-0123-456789abcdef 2>&1)"
rc=$?
set -e
if [[ $rc -ne 2 || "$out" != *timeout-range* ]]; then
    fail "timeout 19 should be refused"
fi

set +e
out="$("$helper" --timeout=46 arm no-such-unit 01234567-89ab-cdef-0123-456789abcdef 2>&1)"
rc=$?
set -e
if [[ $rc -ne 2 || "$out" != *timeout-range* ]]; then
    fail "timeout 46 should be refused"
fi

set +e
out="$("$helper" --user arm cd-missing-unit.service 01234567-89ab-cdef-0123-456789abcdef 2>&1)"
rc=$?
set -e
if [[ $rc -eq 0 ]]; then
    fail "missing unit should not arm"
elif [[ "$out" != *missing-unit* && "$out" != *inactive-unit* ]]; then
    fail "missing unit should report missing-unit or inactive-unit"
fi

if [[ $failures -ne 0 ]]; then
    printf 'test_trial_cutoff: %d failure(s)\n' "$failures" >&2
    exit 1
fi
printf 'test_trial_cutoff: ok\n'
exit 0
