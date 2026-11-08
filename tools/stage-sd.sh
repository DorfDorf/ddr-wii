#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build padtest and the game, and lay them out for the Homebrew Channel (docs/03 §6.3):
#   <dest>/apps/padtest/{boot.dol,meta.xml}
#   <dest>/apps/ddrwii/{boot.dol,meta.xml}
#   <dest>/ddrwii/                      (data folder the game will read songs from)
#
# Usage (devkitPro MSys2 shell):
#   tools/stage-sd.sh            # stage into sd/ in the repo (gitignored boot.dol files)
#   tools/stage-sd.sh /e         # stage straight onto an SD card mounted as E:
# Only the files above are written; nothing else on the card is touched.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
dest="${1:-$root/sd}"

make -C "$root/tools/padtest"
make -C "$root/game"

install_app() {   # <name> <dol> <meta.xml>
    local dir="$dest/apps/$1"
    mkdir -p "$dir"
    cp "$2" "$dir/boot.dol"
    cp "$3" "$dir/meta.xml"
    echo "  $dir/boot.dol  ($(wc -c <"$dir/boot.dol") bytes)"
}

echo "Staging into $dest"
install_app padtest "$root/tools/padtest/padtest.dol" "$root/tools/padtest/meta.xml"
install_app ddrwii   "$root/game/game.dol"            "$root/game/meta.xml"
mkdir -p "$dest/ddrwii/songs"
echo "Done. Copy the apps/ and ddrwii/ folders to the root of the SD card."
