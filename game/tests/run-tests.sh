#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build and run the host-side core tests (M4a): parser + timing vs the simfile
# oracle, unit tests and the fuzz pass. No devkitPro/libogc needed.
#
#   game/tests/run-tests.sh            # from Git Bash, MSYS2 or Linux/macOS
#   game/tests/run-tests.sh clean      # extra arguments go to make
#
# On Windows this needs g++ in devkitPro's MSYS2 (one-time setup, from the
# devkitPro MSYS2 shell: pacman -S gcc). Git Bash can't run that g++ itself
# (different MSYS runtime), so from Git Bash the build is handed to
# devkitPro's bash.
set -e
here=$(cd "$(dirname "$0")" && pwd)
dkp_bash="${DKP_MSYS2:-/c/devkitPro/msys2}/usr/bin/bash.exe"

if [ -x "$dkp_bash" ] && command -v cygpath >/dev/null 2>&1; then
  case "$(cygpath -m /)" in
    */devkitPro/msys2/) ;;  # already inside devkitPro's MSYS2
    *) exec "$dkp_bash" -lc "make -C '$(cygpath -u "$here")' $*" ;;
  esac
fi
exec make -C "$here" "$@"
