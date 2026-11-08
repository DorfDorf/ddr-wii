#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# One-time devkitPro setup for this project (docs/03 §1-3).
# Run from the devkitPro MSys2 shell, or: C:/devkitPro/msys2/usr/bin/bash.exe -lc tools/setup-devkitpro.sh
# Safe to re-run: pacman uses --needed and GRRLIB is rebuilt from a pinned checkout.
set -euo pipefail

: "${DEVKITPRO:?DEVKITPRO is not set - run this from the devkitPro MSys2 shell}"

PAC="pacman --noconfirm --needed"

echo "== Update package database + installed packages =="
pacman --noconfirm -Syu

echo "== Wii toolchain (wii-dev) =="
$PAC -S wii-dev

echo "== Libraries (docs/03 §1 step 5) =="
$PAC -S libfat-ogc ppc-libpng ppc-freetype ppc-libjpeg-turbo ppc-zlib \
        ppc-libmad ppc-libvorbisidec ppc-libogg

echo "== Host tools =="
$PAC -S git make

echo "== GRRLIB (docs/03 §2) =="
GRRLIB_DIR="${GRRLIB_DIR:-$HOME/GRRLIB}"
# Pinned to the version this project is built with: v4.6.1 + 9 commits (2026-10-12).
GRRLIB_REF="${GRRLIB_REF:-a426e6170cbd9a4a69851a705637d753a09f8294}"
if [ ! -d "$GRRLIB_DIR/.git" ]; then
    git clone https://github.com/GRRLIB/GRRLIB.git "$GRRLIB_DIR"
fi
git -C "$GRRLIB_DIR" fetch --tags origin
git -C "$GRRLIB_DIR" checkout "$GRRLIB_REF"
git -C "$GRRLIB_DIR" pull --ff-only 2>/dev/null || true   # only matters when GRRLIB_REF is a branch
make -C "$GRRLIB_DIR/GRRLIB" clean all install   # the Makefile lives in the inner GRRLIB/ folder
echo "GRRLIB at $(git -C "$GRRLIB_DIR" describe --tags --always)"

echo "== Verify =="
echo "DEVKITPRO=$DEVKITPRO  DEVKITPPC=$DEVKITPPC"
"$DEVKITPPC/bin/powerpc-eabi-gcc" --version | head -1
ls "$DEVKITPRO/portlibs/wii/lib/libgrrlib.a"
