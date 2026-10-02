#!/bin/sh
# Run integration tests against a disposable Secret Service, never the desktop keyring.
set -eu
if ! command -v dbus-run-session >/dev/null || ! command -v gnome-keyring-daemon >/dev/null; then
    echo 'Secret Service integration tests need dbus-run-session and gnome-keyring-daemon'
    exit 77
fi
keyring_test_dir=$(mktemp -d)
trap 'rm -rf "$keyring_test_dir"' EXIT HUP INT TERM
export NEKOTUNE_TEST_KEYRING_DIR="$keyring_test_dir"
export QTKEYCHAIN_BACKEND=libsecret
mkdir -m 700 "$keyring_test_dir/runtime"
export XDG_RUNTIME_DIR="$keyring_test_dir/runtime"
unset GNOME_KEYRING_CONTROL
dbus-run-session -- sh -c '
    umask 077
    mkdir -p "$NEKOTUNE_TEST_KEYRING_DIR/data" "$NEKOTUNE_TEST_KEYRING_DIR/control"
    printf "%s\n" "disposable-test-password" | env XDG_DATA_HOME="$NEKOTUNE_TEST_KEYRING_DIR/data" gnome-keyring-daemon --foreground --unlock --components=secrets --control-directory="$NEKOTUNE_TEST_KEYRING_DIR/control" >"$NEKOTUNE_TEST_KEYRING_DIR/daemon.log" 2>&1 &
    keyring_test_pid=$!
    trap '\''kill "$keyring_test_pid" 2>/dev/null || true; wait "$keyring_test_pid" 2>/dev/null || true'\'' EXIT HUP INT TERM
    ready=0
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
        if gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus --method org.freedesktop.DBus.NameHasOwner org.freedesktop.secrets | grep -q true; then
            ready=1
            break
        fi
        sleep 0.1
    done
    test "$ready" = 1
    "$@"
' sh "$@"
