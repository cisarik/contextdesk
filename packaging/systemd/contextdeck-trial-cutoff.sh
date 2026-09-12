#!/usr/bin/env bash
# Invocation-bound trial cutoff for one systemd unit instance.
# Arms a PID1-owned transient timer (user or system bus) that re-checks the
# exact InvocationID and then SIGKILLs only that unit's main process.
# Never uses pkill, killall, a guessed PID, EXTEND_TIMEOUT_USEC, or restart.
set -euo pipefail

usage() {
    printf '%s\n' "usage: $0 [--user] [--timeout=SEC] arm|cancel|status|fire UNIT INVOCATION_ID" >&2
    printf '%s\n' "timeout SEC must be an integer in 20-45 (default 30)." >&2
    printf '%s\n' "setup failure means do not ARM the broker." >&2
}

bus=()
timeout_sec=30
positional=()

for arg in "$@"; do
    case "$arg" in
    --user)
        bus=(--user)
        ;;
    --timeout=*)
        timeout_sec="${arg#--timeout=}"
        ;;
    -h | --help)
        usage
        exit 2
        ;;
    *)
        positional+=("$arg")
        ;;
    esac
done

if [[ ${#positional[@]} -lt 3 ]]; then
    usage
    exit 2
fi

cmd="${positional[0]}"
unit="${positional[1]}"
expected="${positional[2]}"

if [[ ! "$timeout_sec" =~ ^[0-9]+$ ]] || ((timeout_sec < 20 || timeout_sec > 45)); then
    printf '%s\n' "cutoff-refuse reason=timeout-range timeout-sec=${timeout_sec}" >&2
    exit 2
fi

if [[ ! "$unit" =~ ^[A-Za-z0-9:_.@\\-]+$ ]]; then
    printf '%s\n' "cutoff-refuse reason=bad-unit" >&2
    exit 2
fi

if [[ ! "$expected" =~ ^[0-9a-fA-F-]{8,}$ ]]; then
    printf '%s\n' "cutoff-refuse reason=bad-invocation" >&2
    exit 2
fi

if ! command -v systemctl >/dev/null || ! command -v systemd-run >/dev/null; then
    printf '%s\n' "cutoff-refuse reason=systemd-missing" >&2
    exit 1
fi

compact="$(printf '%s' "$expected" | tr -cd '0-9a-fA-F' | cut -c1-16)"
if [[ -z "$compact" ]]; then
    printf '%s\n' "cutoff-refuse reason=bad-invocation" >&2
    exit 2
fi
cutoff_base="cd-trial-${compact}"
cutoff_timer="${cutoff_base}.timer"
cutoff_service="${cutoff_base}.service"
script_path="$(readlink -f "$0")"

sys() {
    systemctl "${bus[@]}" "$@"
}

show_prop() {
    local prop="$1"
    local target="$2"
    sys show -p "$prop" --value "$target" 2>/dev/null || true
}

print_guard() {
    local live_state live_id timer_state accuracy timers
    live_state="$(show_prop ActiveState "$unit")"
    live_id="$(show_prop InvocationID "$unit")"
    timer_state="$(show_prop ActiveState "$cutoff_timer")"
    accuracy="$(show_prop AccuracyUSec "$cutoff_timer")"
    timers="$(show_prop TimersMonotonic "$cutoff_timer")"
    printf '%s\n' "cutoff-status unit=${unit} expected=${expected} live-invocation=${live_id:-none} live-state=${live_state:-unknown} timer=${cutoff_timer} timer-state=${timer_state:-not-found} timers=${timers:-none} accuracy-usec=${accuracy:-none}"
}

load_state() {
    show_prop LoadState "$unit"
}

live_state() {
    show_prop ActiveState "$unit"
}

live_invocation() {
    show_prop InvocationID "$unit"
}

refuse_unless_matching_active() {
    local load state invocation
    load="$(load_state)"
    if [[ -z "$load" || "$load" == "not-found" ]]; then
        printf '%s\n' "cutoff-refuse reason=missing-unit unit=${unit}" >&2
        return 1
    fi
    state="$(live_state)"
    invocation="$(live_invocation)"
    if [[ "$state" != "active" ]]; then
        printf '%s\n' "cutoff-refuse reason=inactive-unit unit=${unit} state=${state:-unknown}" >&2
        return 1
    fi
    if [[ -z "$invocation" ]]; then
        printf '%s\n' "cutoff-refuse reason=missing-invocation unit=${unit}" >&2
        return 1
    fi
    if [[ "$invocation" != "$expected" ]]; then
        printf '%s\n' "cutoff-refuse reason=stale-guard unit=${unit} expected=${expected} live=${invocation}" >&2
        return 1
    fi
    return 0
}

arm_cutoff() {
    if ! refuse_unless_matching_active; then
        return 1
    fi
    if [[ "$(show_prop LoadState "$cutoff_timer")" == "loaded" || "$(show_prop LoadState "$cutoff_service")" == "loaded" ]]; then
        printf '%s\n' "cutoff-refuse reason=cutoff-exists timer=${cutoff_timer}" >&2
        return 1
    fi

    local user_flag=()
    if [[ ${#bus[@]} -gt 0 ]]; then
        user_flag=(--user)
    fi

    if ! systemd-run "${bus[@]}" \
        --quiet \
        --collect \
        --description="ContextDeck trial cutoff" \
        --unit="${cutoff_base}" \
        --on-active="${timeout_sec}s" \
        --timer-property=AccuracySec=1us \
        --timer-property=RandomizedDelaySec=0 \
        --timer-property=Persistent=no \
        --timer-property=RemainAfterElapse=no \
        --property=Type=oneshot \
        --property=Restart=no \
        --property=TimeoutStartSec=5 \
        -- "$script_path" "${user_flag[@]}" fire "$unit" "$expected"; then
        printf '%s\n' "cutoff-refuse reason=timer-setup-failed unit=${unit}" >&2
        return 1
    fi

    local timer_state accuracy timers
    timer_state="$(show_prop ActiveState "$cutoff_timer")"
    accuracy="$(show_prop AccuracyUSec "$cutoff_timer")"
    timers="$(show_prop TimersMonotonic "$cutoff_timer")"
    if [[ "$timer_state" != "active" ]]; then
        printf '%s\n' "cutoff-refuse reason=timer-not-active timer=${cutoff_timer} state=${timer_state:-unknown}" >&2
        return 1
    fi
    if [[ "$accuracy" != "1us" && "$accuracy" != "1" ]]; then
        printf '%s\n' "cutoff-refuse reason=timer-param-mismatch accuracy-usec=${accuracy:-none} expected=1us" >&2
        sys stop "$cutoff_timer" "$cutoff_service" >/dev/null 2>&1 || true
        sys reset-failed "$cutoff_timer" "$cutoff_service" >/dev/null 2>&1 || true
        return 1
    fi
    if [[ "$timers" != *"OnActiveUSec=${timeout_sec}s"* ]]; then
        printf '%s\n' "cutoff-refuse reason=timer-param-mismatch timers=${timers:-none} expected-on-active=${timeout_sec}s" >&2
        sys stop "$cutoff_timer" "$cutoff_service" >/dev/null 2>&1 || true
        sys reset-failed "$cutoff_timer" "$cutoff_service" >/dev/null 2>&1 || true
        return 1
    fi
    printf '%s\n' "cutoff-armed unit=${unit} invocation=${expected} timeout-sec=${timeout_sec} accuracy-usec=1us timer=${cutoff_timer}"
    print_guard
}

cancel_cutoff() {
    sys stop "$cutoff_timer" "$cutoff_service" >/dev/null 2>&1 || true
    sys reset-failed "$cutoff_timer" "$cutoff_service" >/dev/null 2>&1 || true
    printf '%s\n' "cutoff-cancelled timer=${cutoff_timer} unit=${unit} expected=${expected}"
}

fire_cutoff() {
    local load state invocation
    load="$(load_state)"
    state="$(live_state)"
    invocation="$(live_invocation)"
    if [[ -z "$load" || "$load" == "not-found" ]]; then
        printf '%s\n' "cutoff-skip reason=missing-unit unit=${unit} expected=${expected}"
        return 0
    fi
    if [[ "$state" != "active" && "$state" != "activating" && "$state" != "deactivating" ]]; then
        printf '%s\n' "cutoff-skip reason=inactive-unit unit=${unit} state=${state:-unknown} expected=${expected}"
        return 0
    fi
    if [[ -z "$invocation" || "$invocation" != "$expected" ]]; then
        printf '%s\n' "cutoff-skip reason=stale-guard unit=${unit} expected=${expected} live=${invocation:-none}"
        return 0
    fi
    if ! sys kill --kill-whom=main --signal=SIGKILL "$unit"; then
        printf '%s\n' "cutoff-skip reason=kill-failed unit=${unit} invocation=${expected}" >&2
        return 1
    fi
    printf '%s\n' "cutoff-kill unit=${unit} invocation=${expected}"
}

case "$cmd" in
arm)
    arm_cutoff
    ;;
cancel)
    cancel_cutoff
    ;;
status)
    print_guard
    ;;
fire)
    fire_cutoff
    ;;
*)
    usage
    exit 2
    ;;
esac
