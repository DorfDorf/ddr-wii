#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Boot a .dol/.elf in Dolphin. Usage: tools/run-dolphin.sh path/to/app.dol [dolphin args...]
#
# DOLPHIN overrides the emulator path (default: the portable install in
# %USERPROFILE%\Dolphin-x64). Dolphin runs in batch mode (-b), so it closes
# when emulation stops and the next run starts clean.
# Extra arguments go to Dolphin, e.g. per-run config overrides:
#   tools/run-dolphin.sh game/game.dol -C SYSCONF.IPL.E60=False
# From make: make -C game run-dolphin DOLPHIN_ARGS="-C SYSCONF.IPL.E60=False"
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "usage: $0 <file.dol|file.elf>" >&2
    exit 2
fi
if [ ! -f "$1" ]; then
    echo "error: $1 not found (build it first)" >&2
    exit 1
fi

if [ -z "${DOLPHIN:-}" ]; then
    profile="${USERPROFILE:-}"
    if [ -z "$profile" ]; then
        # Some ways of starting the MSys2 shell drop the Windows env; ask Windows directly.
        profile="$(powershell.exe -NoProfile -Command '[Environment]::GetFolderPath("UserProfile")' | tr -d '\r')"
    fi
    DOLPHIN="$(cygpath -u "$profile")/Dolphin-x64/Dolphin.exe"
fi
if [ ! -x "$DOLPHIN" ]; then
    echo "error: Dolphin not found at $DOLPHIN (set DOLPHIN=/path/to/Dolphin.exe)" >&2
    exit 1
fi

# Close a previous instance so each run is a fresh boot.
taskkill //IM Dolphin.exe //F >/dev/null 2>&1 || true

app="$(cygpath -w "$(realpath "$1")")"
shift
echo "Booting $app in Dolphin${*:+ ($*)}"
# Launch through `cmd /c start` so Dolphin is fully detached: a plain `&` leaves it
# holding this shell's output pipe, and make / the VS Code task never return.
# cmd's own stdio goes to /dev/null too, because `start` hands its handles to Dolphin.
cmd.exe //c start "" "$(cygpath -w "$DOLPHIN")" -b "$@" -e "$app" </dev/null >/dev/null 2>&1
