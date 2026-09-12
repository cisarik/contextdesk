#!/bin/sh
# systemd-sleep hook: quiesce an active ContextDeck broker before sleep and
# start it once after resume, always disarmed. systemd invokes:
#   /usr/lib/systemd/system-sleep/contextdeck-broker pre|post <mode>
# Tests may pass --testdir DIR as the first two arguments. That seam is argv
# only; production systemd-sleep never supplies it.
#
# Marker lives in /run/contextdeck-sleep, not RuntimeDirectory=contextdeck,
# because systemd removes the runtime dir when the unit stops.
set -eu

UNIT="contextdeck-broker.service"
MAGIC="CONTEXTDECK_SLEEP_V1"
PROD_SYSTEMCTL="/usr/bin/systemctl"
PROD_MARKER_DIR="/run/contextdeck-sleep"
PROD_MARKER="${PROD_MARKER_DIR}/broker-was-active"
PROD_BOOT_ID_FILE="/proc/sys/kernel/random/boot_id"

log()
{
    printf 'contextdeck-sleep class=%s' "$1"
    if [ "${2:-}" != "" ]; then
        printf ' reason=%s' "$2"
    fi
    printf '\n'
}

is_sleep_mode()
{
    case "$1" in
    suspend | hibernate | hybrid-sleep | suspend-then-hibernate)
        return 0
        ;;
    *)
        return 1
        ;;
    esac
}

refuse_testdir()
{
    log error bad-testdir
    exit 0
}

TESTDIR=""
if [ "${1:-}" = "--testdir" ]; then
    TESTDIR="${2:-}"
    shift 2
    case "$TESTDIR" in
    "" | *..* | [!/]*)
        refuse_testdir
        ;;
    esac
    if [ ! -d "$TESTDIR" ]; then
        refuse_testdir
    fi
    RESOLVED=$(readlink -f "$TESTDIR") || refuse_testdir
    case "$RESOLVED" in
    / | /run | /run/* | /etc | /etc/* | /usr | /usr/* | /lib | /lib/* | /var/run | /var/run/*)
        refuse_testdir
        ;;
    esac
    TESTDIR="$RESOLVED"
fi

PHASE="${1:-}"
MODE="${2:-}"

if [ -n "$TESTDIR" ]; then
    SYSTEMCTL="${TESTDIR}/bin/systemctl"
    MARKER_DIR="${TESTDIR}/run/contextdeck-sleep"
    MARKER="${MARKER_DIR}/broker-was-active"
    BOOT_ID_FILE="${TESTDIR}/boot_id"
    REQUIRE_ROOT=0
else
    SYSTEMCTL="$PROD_SYSTEMCTL"
    MARKER_DIR="$PROD_MARKER_DIR"
    MARKER="$PROD_MARKER"
    BOOT_ID_FILE="$PROD_BOOT_ID_FILE"
    REQUIRE_ROOT=1
fi

TMP_MARKER="${MARKER_DIR}/.tmp"

read_boot_id()
{
    if [ ! -f "$BOOT_ID_FILE" ] || [ -L "$BOOT_ID_FILE" ]; then
        return 1
    fi
    boot=$(tr -d ' \t\r\n' <"$BOOT_ID_FILE")
    case "$boot" in
    "" | *[!0-9a-fA-F-]*)
        return 1
        ;;
    esac
    printf '%s' "$boot"
}

consume_marker()
{
    rm -f "$MARKER" "$TMP_MARKER"
}

marker_present()
{
    [ -e "$MARKER" ] || [ -L "$MARKER" ]
}

valid_marker()
{
    [ -L "$MARKER" ] && return 1
    [ -f "$MARKER" ] || return 1
    mode=$(stat -c '%a' "$MARKER")
    uid=$(stat -c '%u' "$MARKER")
    gid=$(stat -c '%g' "$MARKER")
    [ "$mode" = "600" ] || return 1
    if [ "$REQUIRE_ROOT" -eq 1 ]; then
        [ "$uid" = "0" ] && [ "$gid" = "0" ] || return 1
    fi
    magic=$(sed -n '1p' "$MARKER")
    bootline=$(sed -n '2p' "$MARKER")
    extra=$(sed -n '3p' "$MARKER")
    [ "$extra" = "" ] || return 1
    [ "$magic" = "$MAGIC" ] || return 1
    boot=$(read_boot_id) || return 1
    [ "$bootline" = "boot=${boot}" ] || return 1
}

write_marker()
{
    boot=$(read_boot_id) || return 1
    mkdir -p "$MARKER_DIR" || return 1
    chmod 0700 "$MARKER_DIR" || return 1
    rm -f "$TMP_MARKER"
    umask 077
    printf '%s\nboot=%s\n' "$MAGIC" "$boot" >"$TMP_MARKER" || return 1
    chmod 0600 "$TMP_MARKER" || return 1
    mv -f "$TMP_MARKER" "$MARKER"
}

unit_state()
{
    if [ ! -x "$SYSTEMCTL" ]; then
        printf '%s\n' ""
        return 0
    fi
    "$SYSTEMCTL" show --property=ActiveState --value -- "$UNIT" 2>/dev/null || printf '%s\n' ""
}

if ! is_sleep_mode "$MODE"; then
    log skip not-sleep-mode
    exit 0
fi

case "$PHASE" in
pre)
    if marker_present && ! valid_marker; then
        consume_marker
        log error bad-marker
    fi
    state=$(unit_state)
    if [ "$state" != "active" ]; then
        log skip inactive-unit
        exit 0
    fi
    if ! write_marker; then
        log error marker-write
        "$SYSTEMCTL" stop -- "$UNIT" >/dev/null 2>&1 || true
        consume_marker
        exit 0
    fi
    state=$(unit_state)
    if [ "$state" != "active" ]; then
        consume_marker
        log skip concurrent-change
        exit 0
    fi
    if ! "$SYSTEMCTL" stop -- "$UNIT" >/dev/null 2>&1; then
        consume_marker
        log error stop-failed
        exit 0
    fi
    state=$(unit_state)
    if [ "$state" != "inactive" ]; then
        consume_marker
        log error stop-failed
        exit 0
    fi
    log stop
    exit 0
    ;;
post)
    if ! marker_present; then
        log skip no-marker
        exit 0
    fi
    if ! valid_marker; then
        consume_marker
        log error bad-marker
        exit 0
    fi
    consume_marker
    state=$(unit_state)
    if [ "$state" != "inactive" ]; then
        log skip not-inactive
        exit 0
    fi
    if ! "$SYSTEMCTL" start -- "$UNIT" >/dev/null 2>&1; then
        log error start-failed
        exit 0
    fi
    log start
    exit 0
    ;;
*)
    log skip not-sleep-mode
    exit 0
    ;;
esac
