#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build padtest and the game, and lay them out for the Homebrew Channel (docs/03 §6.3):
#   <dest>/apps/padtest/{boot.dol,meta.xml}
#   <dest>/apps/ddrwii/{boot.dol,meta.xml}
#   <dest>/ddrwii/                      (data folder the game will read songs from)
#   <dest>/ddrwii/test/click120-*.wav   (M4b click tracks, tools/make-click-track.py; made once)
#   <dest>/ddrwii/songs/ddr-wii tests/Click 120/   (M4c test song, tools/make-test-song.py)
#
# Usage (devkitPro MSys2 shell):
#   tools/stage-sd.sh            # stage into sd/ in the repo (gitignored boot.dol files)
#   tools/stage-sd.sh /e         # stage straight onto an SD card mounted as E:
#   tools/stage-sd.sh "$(cygpath -u "$USERPROFILE")/Dolphin-x64/User/Load/WiiSDSync"
#                                # Dolphin's SD sync folder (doc 03 §4.3)
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
mkdir -p "$dest/ddrwii/songs" "$dest/ddrwii/test"

# Click tracks: 5 minutes at 120 BPM, 48 and 44.1 kHz (~58 + 53 MB). Python 3 from Windows
# (py) or the shell; skipped if they're already there.
py=""
for p in py python3 python; do command -v "$p" >/dev/null 2>&1 && { py="$p"; break; }; done
winpath() { if command -v cygpath >/dev/null 2>&1; then cygpath -w "$1"; else echo "$1"; fi; }
for rate in 48000 44100; do
    out="$dest/ddrwii/test/click120-$((rate / 1000))k.wav"
    [ -f "$out" ] && continue
    if [ -z "$py" ]; then echo "  (no python: skipped $out)"; continue; fi
    "$py" "$(winpath "$root/tools/make-click-track.py")" "$(winpath "$out")" --rate "$rate"
done

# Encoded copies for the decoders (doc 05 M4b), made from the 44.1 kHz WAV with ffmpeg
# (winget install Gyan.FFmpeg; found on PATH or in winget's package folder).
ffmpeg="$(command -v ffmpeg || true)"
[ -n "$ffmpeg" ] || ffmpeg="$(ls /c/Users/*/AppData/Local/Microsoft/WinGet/Packages/Gyan.FFmpeg*/*/bin/ffmpeg.exe 2>/dev/null | head -n 1)"
wav="$dest/ddrwii/test/click120-44k.wav"
encode() {   # <extension> <ffmpeg codec options...>
    local out="$dest/ddrwii/test/click120-44k.$1"
    shift
    if [ -f "$out" ]; then return; fi
    if [ -z "$ffmpeg" ] || [ ! -f "$wav" ]; then echo "  (no ffmpeg or WAV: skipped $out)"; return; fi
    "$ffmpeg" -loglevel error -y -i "$(winpath "$wav")" "$@" "$(winpath "$out")" && echo "  $out"
}
encode ogg -c:a libvorbis -q:a 6
encode mp3 -c:a libmp3lame -b:a 192k   # CBR: LAME writes an "Info" tag (see WiiAudio Mp3Source)

# M4c test song: a generated 120 BPM chart on the OGG click track (tools/make-test-song.py).
song="$dest/ddrwii/songs/ddr-wii tests/Click 120"
if [ -n "$py" ] && [ -f "$dest/ddrwii/test/click120-44k.ogg" ]; then
    mkdir -p "$song"
    "$py" "$(winpath "$root/tools/make-test-song.py")" "$(winpath "$song")"
    cp "$dest/ddrwii/test/click120-44k.ogg" "$song/"
else
    echo "  (no python or OGG: skipped $song)"
fi
echo "Done. Copy the apps/ and ddrwii/ folders to the root of the SD card."
