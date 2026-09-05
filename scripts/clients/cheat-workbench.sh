#!/usr/bin/env bash
# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#
# Starts dosbox-automation with the web API on and a fresh token, then
# prints the Cheat Workbench URL, a link carrying the token for a browser
# on another machine, and the token itself.
#
# Usage: cheat-workbench.sh [nowait]
#   DOSBOX_BIN   emulator to start (default: the one shipped next to this script)
#   DOSBOX_PORT  API port (default 8386)
set -euo pipefail

port="${DOSBOX_PORT:-8386}"
script_dir="$(cd "$(dirname "$0")" && pwd)"

find_engine() {
    if [[ -n "${DOSBOX_BIN:-}" ]]; then
        printf '%s\n' "$DOSBOX_BIN"
        return
    fi
    # Inside the AppImage the runtime sets APPIMAGE; the engine is reached
    # through it so the bundled libraries stay in play.
    if [[ -n "${APPIMAGE:-}" && -x "$APPIMAGE" ]]; then
        printf '%s\n' "$APPIMAGE"
        return
    fi
    if [[ -n "${APPDIR:-}" && -x "$APPDIR/AppRun" ]]; then
        printf '%s\n' "$APPDIR/AppRun"
        return
    fi
    local candidate
    for candidate in "$script_dir/dosbox-automation" "$script_dir/bin/dosbox" "$script_dir/dosbox"; do
        if [[ -x "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return
        fi
    done
    for candidate in dosbox-automation dosbox; do
        if command -v "$candidate" >/dev/null 2>&1; then
            command -v "$candidate"
            return
        fi
    done
    return 1
}

make_token() {
    if command -v openssl >/dev/null 2>&1; then
        openssl rand -hex 32
    else
        od -An -N32 -tx1 /dev/urandom | tr -d ' \n'
    fi
}

engine="$(find_engine)" || {
    printf 'error: no dosbox-automation binary found next to this script or on PATH; set DOSBOX_BIN\n' >&2
    exit 1
}

token="$(make_token)"
if [[ ! "$token" =~ ^[0-9a-f]{64}$ ]]; then
    printf 'error: token generation failed\n' >&2
    exit 1
fi

# The conf stays behind on purpose: the engine re-reads it when the
# restart hotkey re-execs, which would fail against a deleted file.
# The runtime dir is per user and cleared at logout.
conf_dir="$(mktemp -d "${XDG_RUNTIME_DIR:-${TMPDIR:-/tmp}}/dosbox-automation-workbench.XXXXXX")"
conf="$conf_dir/workbench.conf"
cat > "$conf" <<CONF
[webserver]
webserver_enabled = on
webserver_port = $port
CONF

export DOSBOX_API_TOKEN="$token"
"$engine" -conf "$conf" >"$conf_dir/engine.log" 2>&1 &
engine_pid=$!

url="http://127.0.0.1:$port/tools/cheat-workbench.html"
printf '\n'
printf 'dosbox-automation started (pid %s), log: %s\n' "$engine_pid" "$conf_dir/engine.log"
printf '\n'
printf '  Cheat Workbench:  %s\n' "$url"
printf '  Link with token:  %s#token=%s\n' "$url" "$token"
printf '  API token:        %s\n' "$token"
printf '\n'
printf 'A browser on this machine connects on its own; type WORKBENCH at the DOS\n'
printf 'prompt or press Ctrl+Alt+W to open it. The link is for a browser elsewhere.\n'
printf 'The emulator keeps running when this window closes; stop it from the\n'
printf 'Workbench setup or by closing its window.\n'
printf '\n'

if [[ "${1:-}" != "nowait" ]]; then
    read -r -n 1 -s -p "press any key to close this window..."
    printf '\n'
fi
