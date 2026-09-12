#!/usr/bin/env bash
# Device-free systemd --user rehearsal of the invocation-bound cutoff helper.
# Unique nonce fixtures. No broker, input, uinput, or production unit names.
set -euo pipefail

root="$(readlink -f "${1:-.}")"
helper="${root}/packaging/systemd/contextdeck-trial-cutoff.sh"
timeout_sec="${CONTEXTDECK_CUTOFF_SEC:-30}"
nonce="$(date +%s%N | tr -cd '0-9' | cut -c1-16)"
fixture="cd-r2-${nonce}"
unit="${fixture}.service"
tmp_unit=""
failures=0
started=0

log() {
    printf '%s\n' "$*"
}

fail() {
    printf 'FAIL %s\n' "$*" >&2
    failures=$((failures + 1))
}

cleanup() {
    if [[ -n "$helper" && -n "$unit" ]]; then
        invocation="$(systemctl --user show -p InvocationID --value "$unit" 2>/dev/null || true)"
        if [[ -n "${invocation:-}" ]]; then
            "$helper" --user cancel "$unit" "$invocation" >/dev/null 2>&1 || true
        fi
    fi
    systemctl --user stop "$unit" >/dev/null 2>&1 || true
    systemctl --user reset-failed "$unit" >/dev/null 2>&1 || true
    systemctl --user list-units --all --plain --no-legend 'cd-r2-*' 2>/dev/null | awk '{print $1}' | while read -r leftover; do
        [[ -z "$leftover" ]] && continue
        systemctl --user stop "$leftover" >/dev/null 2>&1 || true
        systemctl --user reset-failed "$leftover" >/dev/null 2>&1 || true
    done
    systemctl --user list-units --all --plain --no-legend 'cd-trial-*' 2>/dev/null | awk '{print $1}' | while read -r leftover; do
        [[ -z "$leftover" ]] && continue
        systemctl --user stop "$leftover" >/dev/null 2>&1 || true
        systemctl --user reset-failed "$leftover" >/dev/null 2>&1 || true
    done
    if [[ -n "$tmp_unit" ]]; then
        rm -f "$tmp_unit"
    fi
}

trap cleanup EXIT

start_fixture() {
    systemd-run --user \
        --unit="$fixture" \
        --service-type=notify \
        --property=NotifyAccess=all \
        --property=Restart=no \
        --property=TimeoutStartSec=5 \
        /bin/sh -c 'systemd-notify --ready; exec sleep 3600' >/dev/null
    started=1
}

stop_fixture() {
    systemctl --user stop "$unit" >/dev/null 2>&1 || true
    systemctl --user reset-failed "$unit" >/dev/null 2>&1 || true
    started=0
}

invocation_of() {
    systemctl --user show -p InvocationID --value "$1"
}

state_of() {
    systemctl --user show -p ActiveState --value "$1"
}

wait_until() {
    local n=0
    local max="${2:-80}"
    while ((n < max)); do
        if eval "$1"; then
            return 0
        fi
        sleep 0.5
        n=$((n + 1))
    done
    return 1
}

bash -n "$helper"
chmod +x "$helper"

tmp_unit="$(mktemp /tmp/cd-r2-verify-XXXXXX.service)"
cat >"$tmp_unit" <<'EOF'
[Unit]
Description=ContextDeck device-free cutoff rehearsal fixture

[Service]
Type=notify
NotifyAccess=all
Restart=no
TimeoutStartSec=5
ExecStart=/bin/true
EOF
if ! systemd-analyze verify "$tmp_unit" >/tmp/cd-r2-verify.out 2>&1; then
    if ! grep -E 'Command line.*is not executable|Failed to prepare filename' /tmp/cd-r2-verify.out >/dev/null; then
        cat /tmp/cd-r2-verify.out >&2
        fail "fixture systemd-analyze verify"
    fi
fi
rm -f /tmp/cd-r2-verify.out

log "case=normal-completion"
start_fixture
id="$(invocation_of "$unit")"
state="$(state_of "$unit")"
log "fixture-state=${state} invocation=${id}"
if [[ "$state" != "active" || -z "$id" ]]; then
    fail "fixture did not become active"
else
    if ! out="$("$helper" --user --timeout="${timeout_sec}" arm "$unit" "$id")"; then
        fail "arm for normal completion"
    else
        log "$out"
        if [[ "$out" != *"timeout-sec=${timeout_sec}"* || "$out" != *"accuracy-usec=1us"* ]]; then
            fail "timer parameters missing from arm output"
        fi
        if ! "$helper" --user cancel "$unit" "$id"; then
            fail "cancel after normal completion"
        fi
        stop_fixture
        if [[ "$(state_of "$unit")" == "active" ]]; then
            fail "fixture still active after normal stop"
        fi
        log "cutoff-action=cancelled-before-expiry cleanup=ok"
    fi
fi
stop_fixture

log "case=exited-fixture"
start_fixture
id="$(invocation_of "$unit")"
stop_fixture
set +e
out="$("$helper" --user --timeout="${timeout_sec}" arm "$unit" "$id" 2>&1)"
rc=$?
set -e
log "$out"
if [[ $rc -eq 0 ]]; then
    fail "arm succeeded on exited fixture"
else
    log "cutoff-action=refused-inactive cleanup=ok"
fi

log "case=hung-fixture"
start_fixture
id="$(invocation_of "$unit")"
if ! out="$("$helper" --user --timeout="${timeout_sec}" arm "$unit" "$id")"; then
    fail "arm for hung fixture"
else
    log "$out"
    if wait_until "[[ \$(state_of '$unit') != active ]]" $((timeout_sec * 4 + 20)); then
        log "cutoff-action=kill live-state=$(state_of "$unit") cleanup=pending"
    else
        fail "hung fixture was not killed"
    fi
    "$helper" --user cancel "$unit" "$id" >/dev/null 2>&1 || true
fi
stop_fixture

log "case=shell-loss"
start_fixture
id="$(invocation_of "$unit")"
fifo="$(mktemp -u /tmp/cd-r2-fifo-XXXXXX)"
mkfifo "$fifo"
(
    if "$helper" --user --timeout="${timeout_sec}" arm "$unit" "$id" >"$fifo.out" 2>&1; then
        printf 'armed\n' >"$fifo"
        sleep 3600
    else
        printf 'arm-failed\n' >"$fifo"
    fi
) &
invoker=$!
if ! armed_msg="$(timeout 10 cat "$fifo")"; then
    fail "invoking shell did not report arm"
    kill -KILL "$invoker" >/dev/null 2>&1 || true
else
    log "invoking-shell=${armed_msg}"
    kill -KILL "$invoker" >/dev/null 2>&1 || true
    wait "$invoker" >/dev/null 2>&1 || true
    if [[ "$armed_msg" != "armed" ]]; then
        fail "arm failed before shell-loss"
        cat "$fifo.out" >&2 || true
    elif wait_until "[[ \$(state_of '$unit') != active ]]" $((timeout_sec * 4 + 20)); then
        log "cutoff-action=kill-after-shell-loss live-state=$(state_of "$unit")"
    else
        fail "cutoff did not fire after shell-loss"
    fi
fi
rm -f "$fifo" "$fifo.out"
"$helper" --user cancel "$unit" "$id" >/dev/null 2>&1 || true
stop_fixture

log "case=timer-setup-failure"
set +e
out="$("$helper" --user --timeout="${timeout_sec}" arm 'cd-r2-missing.service' 01234567-89ab-cdef-0123-456789abcdef 2>&1)"
rc=$?
set -e
log "$out"
if [[ $rc -eq 0 ]]; then
    fail "arm succeeded without a unit"
else
    log "cutoff-action=setup-failure-do-not-arm"
fi

log "case=stale-guard"
start_fixture
id1="$(invocation_of "$unit")"
if ! out="$("$helper" --user --timeout="${timeout_sec}" arm "$unit" "$id1")"; then
    fail "arm for stale-guard"
else
    log "$out"
    stop_fixture
    start_fixture
    id2="$(invocation_of "$unit")"
    log "live-invocation=${id2} guarded-invocation=${id1}"
    if [[ "$id1" == "$id2" ]]; then
        fail "restart did not change invocation"
    fi
    if wait_until "[[ \$(state_of '$unit') != active ]]" $((timeout_sec * 4 + 20)); then
        fail "stale cutoff killed the new invocation"
    else
        log "cutoff-action=stale-skip live-state=$(state_of "$unit")"
    fi
    "$helper" --user cancel "$unit" "$id1" >/dev/null 2>&1 || true
    "$helper" --user cancel "$unit" "$id2" >/dev/null 2>&1 || true
fi
stop_fixture

leftover_r2="$(systemctl --user list-units --all --plain --no-legend 'cd-r2-*' 2>/dev/null | awk '{print $1}' || true)"
leftover_trial="$(systemctl --user list-units --all --plain --no-legend 'cd-trial-*' 2>/dev/null | awk '{print $1}' || true)"
if [[ -n "${leftover_r2}${leftover_trial}" ]]; then
    log "leftovers r2=${leftover_r2:-none} trial=${leftover_trial:-none}"
    cleanup
    leftover_r2="$(systemctl --user list-units --all --plain --no-legend 'cd-r2-*' 2>/dev/null | awk '{print $1}' || true)"
    leftover_trial="$(systemctl --user list-units --all --plain --no-legend 'cd-trial-*' 2>/dev/null | awk '{print $1}' || true)"
fi
if [[ -n "${leftover_r2}${leftover_trial}" ]]; then
    fail "leftover units remain"
else
    log "cleanup-result=none-remain"
fi

if [[ $failures -ne 0 ]]; then
    printf 'rehearse_trial_cutoff: %d failure(s)\n' "$failures" >&2
    exit 1
fi
printf 'rehearse_trial_cutoff: ok timeout-sec=%s\n' "$timeout_sec"
exit 0
