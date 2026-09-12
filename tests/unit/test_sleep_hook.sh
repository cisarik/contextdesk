#!/usr/bin/env bash
# Device-free simulation of packaging/systemd/contextdeck-sleep.sh.
# Never calls the host systemctl, never writes /run, never sleeps the machine.
set -euo pipefail

root="${1:-}"
if [[ -z "$root" ]]; then
    echo "usage: $0 SOURCE_DIR" >&2
    exit 2
fi

hook="${root}/packaging/systemd/contextdeck-sleep.sh"
unit_file="${root}/packaging/systemd/contextdeck-broker.service"
failures=0

fail() {
    printf 'FAIL %s\n' "$*" >&2
    failures=$((failures + 1))
}

pass() {
    printf 'ok %s\n' "$*"
}

sh -n "$hook"
bash -n "$hook"
if [[ ! -x "$hook" ]]; then
    chmod +x "$hook"
fi
if [[ ! -x "$hook" ]]; then
    fail "hook not executable"
fi

grep -q '/usr/bin/systemctl' "$hook" || fail "production systemctl path"
grep -q '/run/contextdeck-sleep' "$hook" || fail "marker dir outside RuntimeDirectory"
if grep -q '/run/contextdeck/broker-was-active' "$hook"; then
    fail "marker must not live in RuntimeDirectory"
fi
if grep -q 'Restart=no' "$hook"; then
    fail "hook must not mention Restart="
fi
if grep -q 'systemctl enable' "$hook"; then
    fail "hook must not enable units"
fi
if grep -q 'systemctl restart' "$hook"; then
    fail "hook must not restart units"
fi

grep -q '^Type=notify$' "$unit_file" || fail "Type=notify"
grep -q '^NotifyAccess=main$' "$unit_file" || fail "NotifyAccess=main"
grep -q '^WatchdogSec=2$' "$unit_file" || fail "WatchdogSec=2"
grep -q '^TimeoutStopSec=5$' "$unit_file" || fail "TimeoutStopSec=5"
grep -q '^Restart=no$' "$unit_file" || fail "Restart=no"
grep -q '^RuntimeDirectoryMode=0755$' "$unit_file" || fail "RuntimeDirectoryMode"
if grep -q '^\[Install\]' "$unit_file"; then
    fail "Install section must not exist"
fi
if grep -E '^(RuntimeMaxSec|EXTEND_TIMEOUT)' "$unit_file"; then
    fail "forbidden lifetime extension in unit"
fi

install_fake_systemctl() {
    local td="$1"
    cat >"${td}/bin/systemctl" <<'EOF'
#!/bin/sh
set -eu
SELF=$(readlink -f "$0")
BIN=$(dirname "$SELF")
TD=$(dirname "$BIN")
printf '%s\n' "$*" >>"${TD}/systemctl.log"

cmd=""
unit=""
for arg in "$@"; do
    case "$arg" in
    show | stop | start)
        cmd=$arg
        ;;
    --property=ActiveState | --value | --) ;;
    contextdeck-broker.service)
        unit=$arg
        ;;
    *)
        printf '%s\n' "$arg" >>"${TD}/unexpected.log"
        ;;
    esac
done

if [ "$unit" != "contextdeck-broker.service" ]; then
    printf 'bad-unit\n' >>"${TD}/unexpected.log"
    exit 1
fi

case "$cmd" in
show)
    sed -n '1p' "${TD}/active-state"
    if [ -f "${TD}/concurrent-after-show" ]; then
        cp "${TD}/concurrent-after-show" "${TD}/active-state"
        rm -f "${TD}/concurrent-after-show"
    fi
    ;;
stop)
    if [ -f "${TD}/fail-stop" ]; then
        if [ -f "${TD}/fail-stop-state" ]; then
            cp "${TD}/fail-stop-state" "${TD}/active-state"
        fi
        exit 1
    fi
    printf 'inactive\n' >"${TD}/active-state"
    printf '0\n' >"${TD}/armed"
    printf '0\n' >"${TD}/lease"
    ;;
start)
    if [ -f "${TD}/fail-start" ]; then
        printf 'failed\n' >"${TD}/active-state"
        printf 'start-failed\n' >>"${TD}/starts.log"
        exit 1
    fi
    printf 'active\n' >"${TD}/active-state"
    printf '0\n' >"${TD}/armed"
    printf '0\n' >"${TD}/lease"
    printf 'start-disarmed\n' >>"${TD}/starts.log"
    ;;
*)
    printf 'bad-cmd\n' >>"${TD}/unexpected.log"
    exit 1
    ;;
esac
EOF
    chmod +x "${td}/bin/systemctl"
}

new_td() {
    local td
    td="$(mktemp -d "${TMPDIR:-/tmp}/cd-sleep-XXXXXX")"
    mkdir -p "${td}/bin" "${td}/run"
    printf '01234567-89ab-cdef-0123-456789abcdef\n' >"${td}/boot_id"
    printf 'inactive\n' >"${td}/active-state"
    printf '0\n' >"${td}/armed"
    printf '0\n' >"${td}/lease"
    : >"${td}/systemctl.log"
    install_fake_systemctl "$td"
    printf '%s' "$td"
}

run_hook() {
    local td="$1"
    shift
    "$hook" --testdir "$td" "$@"
}

expect_class() {
    local out="$1"
    local class="$2"
    local reason="${3:-}"
    if [[ "$out" != *"class=${class}"* ]]; then
        fail "expected class=${class} in: $out"
        return
    fi
    if [[ -n "$reason" && "$out" != *"reason=${reason}"* ]]; then
        fail "expected reason=${reason} in: $out"
    fi
}

assert_no_unexpected() {
    local td="$1"
    if [[ -s "${td}/unexpected.log" ]]; then
        fail "unexpected systemctl args: $(cat "${td}/unexpected.log")"
    fi
}

assert_cmd_args() {
    local td="$1"
    local line
    while IFS= read -r line; do
        case "$line" in
        "show --property=ActiveState --value -- contextdeck-broker.service") ;;
        "stop -- contextdeck-broker.service") ;;
        "start -- contextdeck-broker.service") ;;
        "") ;;
        *)
            fail "unexpected systemctl argv: ${line}"
            ;;
        esac
    done <"${td}/systemctl.log"
}

marker_path() {
    printf '%s/run/contextdeck-sleep/broker-was-active' "$1"
}

start_count() {
    local td="$1"
    if [[ ! -f "${td}/starts.log" ]]; then
        printf '0'
        return
    fi
    grep -c 'start-disarmed' "${td}/starts.log" || true
}

# 1. non-sleep modes are no-ops and never call systemctl.
td="$(new_td)"
printf 'active\n' >"${td}/active-state"
for mode in reboot freeze kexec halt poweroff ''; do
    out="$(run_hook "$td" pre "$mode")"
    expect_class "$out" skip not-sleep-mode
    out="$(run_hook "$td" post "$mode")"
    expect_class "$out" skip not-sleep-mode
done
if [[ -s "${td}/systemctl.log" ]]; then
    fail "non-sleep mode called systemctl"
fi
if [[ -e "$(marker_path "$td")" ]]; then
    fail "non-sleep mode wrote a marker"
fi
pass "non-sleep modes are no-ops"
rm -rf "$td"

# 2. inactive / failed / activating never write a restart marker.
for state in inactive failed activating deactivating; do
    td="$(new_td)"
    printf '%s\n' "$state" >"${td}/active-state"
    out="$(run_hook "$td" pre suspend)"
    expect_class "$out" skip inactive-unit
    if [[ -e "$(marker_path "$td")" ]]; then
        fail "marker created for state=${state}"
    fi
    out="$(run_hook "$td" post suspend)"
    expect_class "$out" skip no-marker
    if [[ "$(start_count "$td")" != "0" ]]; then
        fail "start for inactive state=${state}"
    fi
    rm -rf "$td"
done
pass "inactive/failed/activating produce no marker"

# 3. active unarmed broker: record, stop, resume once, disarmed.
td="$(new_td)"
printf 'active\n' >"${td}/active-state"
printf '0\n' >"${td}/armed"
out="$(run_hook "$td" pre suspend)"
expect_class "$out" stop
[[ -f "$(marker_path "$td")" ]] || fail "missing marker after pre"
[[ "$(stat -c '%a' "$(marker_path "$td")")" == "600" ]] || fail "marker mode"
[[ "$(stat -c '%a' "$(dirname "$(marker_path "$td")")")" == "700" ]] || fail "marker dir mode"
[[ "$(cat "${td}/active-state")" == "inactive" ]] || fail "pre did not stop"
out="$(run_hook "$td" post suspend)"
expect_class "$out" start
[[ ! -e "$(marker_path "$td")" ]] || fail "marker not consumed"
[[ "$(cat "${td}/active-state")" == "active" ]] || fail "post did not start"
[[ "$(cat "${td}/armed")" == "0" ]] || fail "post-start armed"
[[ "$(cat "${td}/lease")" == "0" ]] || fail "post-start lease"
[[ "$(start_count "$td")" == "1" ]] || fail "expected one start"
assert_cmd_args "$td"
assert_no_unexpected "$td"
pass "active unarmed records, stops, resumes once disarmed"
rm -rf "$td"

# 4. active armed broker uses the same stop path; resume is disarmed / no lease.
td="$(new_td)"
printf 'active\n' >"${td}/active-state"
printf '1\n' >"${td}/armed"
printf '1\n' >"${td}/lease"
out="$(run_hook "$td" pre hibernate)"
expect_class "$out" stop
[[ "$(cat "${td}/armed")" == "0" ]] || fail "stop did not drop armed"
[[ "$(cat "${td}/lease")" == "0" ]] || fail "stop did not drop lease"
out="$(run_hook "$td" post hibernate)"
expect_class "$out" start
[[ "$(cat "${td}/armed")" == "0" ]] || fail "armed after resume start"
[[ "$(cat "${td}/lease")" == "0" ]] || fail "lease after resume start"
[[ "$(start_count "$td")" == "1" ]] || fail "armed path start count"
if grep -Eq 'LEASE|ARM' "${td}/systemctl.log"; then
    fail "LEASE/ARM in systemctl argv"
fi
pass "active armed takes the same orderly stop; resume disarmed"
rm -rf "$td"

# 5. failed pre-stop never causes post-start.
td="$(new_td)"
printf 'active\n' >"${td}/active-state"
: >"${td}/fail-stop"
printf 'failed\n' >"${td}/fail-stop-state"
out="$(run_hook "$td" pre suspend)"
expect_class "$out" error stop-failed
[[ ! -e "$(marker_path "$td")" ]] || fail "marker remains after failed stop"
out="$(run_hook "$td" post suspend)"
expect_class "$out" skip no-marker
[[ "$(start_count "$td")" == "0" ]] || fail "start after failed pre-stop"
pass "failed pre-stop never starts"
rm -rf "$td"

td="$(new_td)"
printf 'active\n' >"${td}/active-state"
# stop "succeeds" but leaves a non-inactive unit (hung / failed).
install_fake_systemctl "$td"
cat >"${td}/bin/systemctl" <<'EOF'
#!/bin/sh
set -eu
SELF=$(readlink -f "$0")
TD=$(dirname "$(dirname "$SELF")")
printf '%s\n' "$*" >>"${TD}/systemctl.log"
cmd=""
for arg in "$@"; do
    case "$arg" in
    show | stop | start) cmd=$arg ;;
    esac
done
case "$cmd" in
show) sed -n '1p' "${TD}/active-state" ;;
stop) printf 'failed\n' >"${TD}/active-state" ;;
start) printf 'start-disarmed\n' >>"${TD}/starts.log" ;;
esac
EOF
chmod +x "${td}/bin/systemctl"
out="$(run_hook "$td" pre hybrid-sleep)"
expect_class "$out" error stop-failed
[[ ! -e "$(marker_path "$td")" ]] || fail "marker remains when stop left failed"
out="$(run_hook "$td" post hybrid-sleep)"
expect_class "$out" skip no-marker
[[ "$(start_count "$td")" == "0" ]] || fail "start after non-inactive stop"
pass "non-inactive stop result never starts"
rm -rf "$td"

# 6. post-start failure is bounded and non-retrying.
td="$(new_td)"
printf 'active\n' >"${td}/active-state"
run_hook "$td" pre suspend-then-hibernate >/dev/null
: >"${td}/fail-start"
out="$(run_hook "$td" post suspend-then-hibernate)"
expect_class "$out" error start-failed
[[ "$(cat "${td}/active-state")" == "failed" ]] || fail "failed start state"
[[ ! -e "$(marker_path "$td")" ]] || fail "marker remains after start failure"
out="$(run_hook "$td" post suspend-then-hibernate)"
expect_class "$out" skip no-marker
start_fail_lines="$(grep -c 'start-failed' "${td}/starts.log" || true)"
[[ "$start_fail_lines" == "1" ]] || fail "start retried after failure (${start_fail_lines})"
pass "post-start failure is bounded"
rm -rf "$td"

# 7. duplicate phases, stale/malformed marker, concurrent change, reboot-like absence.
td="$(new_td)"
printf 'active\n' >"${td}/active-state"
run_hook "$td" pre suspend >/dev/null
[[ -f "$(marker_path "$td")" ]] || fail "marker missing before duplicate pre"
out="$(run_hook "$td" pre suspend)"
expect_class "$out" skip inactive-unit
[[ -f "$(marker_path "$td")" ]] || fail "duplicate pre removed marker"
out="$(run_hook "$td" post suspend)"
expect_class "$out" start
out="$(run_hook "$td" post suspend)"
expect_class "$out" skip no-marker
[[ "$(start_count "$td")" == "1" ]] || fail "duplicate post started again"
pass "duplicate pre/post fail closed"
rm -rf "$td"

td="$(new_td)"
mkdir -p "${td}/run/contextdeck-sleep"
printf 'garbage\n' >"$(marker_path "$td")"
chmod 0600 "$(marker_path "$td")"
out="$(run_hook "$td" post suspend)"
expect_class "$out" error bad-marker
[[ ! -e "$(marker_path "$td")" ]] || fail "malformed marker not removed"
[[ "$(start_count "$td")" == "0" ]] || fail "start from malformed marker"
pass "malformed marker fail closed"
rm -rf "$td"

td="$(new_td)"
mkdir -p "${td}/run/contextdeck-sleep"
printf 'CONTEXTDECK_SLEEP_V1\nboot=00000000-0000-0000-0000-000000000000\n' >"$(marker_path "$td")"
chmod 0600 "$(marker_path "$td")"
out="$(run_hook "$td" post suspend)"
expect_class "$out" error bad-marker
[[ ! -e "$(marker_path "$td")" ]] || fail "stale boot-id marker not removed"
[[ "$(start_count "$td")" == "0" ]] || fail "start from stale marker"
pass "stale marker fail closed"
rm -rf "$td"

td="$(new_td)"
printf 'active\n' >"${td}/active-state"
printf 'inactive\n' >"${td}/concurrent-after-show"
out="$(run_hook "$td" pre suspend)"
expect_class "$out" skip concurrent-change
[[ ! -e "$(marker_path "$td")" ]] || fail "concurrent change left marker"
if grep -q '^stop ' "${td}/systemctl.log"; then
    fail "concurrent change issued stop"
fi
out="$(run_hook "$td" post suspend)"
expect_class "$out" skip no-marker
[[ "$(start_count "$td")" == "0" ]] || fail "start after concurrent change"
pass "concurrent state change fail closed"
rm -rf "$td"

td="$(new_td)"
out="$(run_hook "$td" post suspend)"
expect_class "$out" skip no-marker
[[ "$(start_count "$td")" == "0" ]] || fail "reboot-like post started"
pass "reboot-like marker absence fail closed"
rm -rf "$td"

# 8. marker permissions and production path restrictions.
td="$(new_td)"
mkdir -p "${td}/run/contextdeck-sleep"
printf 'CONTEXTDECK_SLEEP_V1\nboot=01234567-89ab-cdef-0123-456789abcdef\n' >"$(marker_path "$td")"
chmod 0666 "$(marker_path "$td")"
out="$(run_hook "$td" post suspend)"
expect_class "$out" error bad-marker
[[ ! -e "$(marker_path "$td")" ]] || fail "mode-0666 marker not removed"
[[ "$(start_count "$td")" == "0" ]] || fail "start from world-readable marker"
pass "marker mode 0666 rejected"
rm -rf "$td"

td="$(new_td)"
mkdir -p "${td}/run/contextdeck-sleep"
ln -s /tmp/contextdeck-sleep-symlink-target "$(marker_path "$td")"
out="$(run_hook "$td" post suspend)"
expect_class "$out" error bad-marker
[[ "$(start_count "$td")" == "0" ]] || fail "start from symlink marker"
pass "symlink marker rejected"
rm -rf "$td"

# --testdir must refuse production paths (do not touch /run).
out="$("$hook" --testdir /run pre suspend 2>/dev/null || true)"
expect_class "$out" error bad-testdir
pass "testdir cannot target /run"

# 9 is covered by test_broker_ipc_client (reconnect never emits LEASE/ARM).

if [[ $failures -ne 0 ]]; then
    printf 'test_sleep_hook: %d failure(s)\n' "$failures" >&2
    exit 1
fi
printf 'test_sleep_hook: ok\n'
exit 0
