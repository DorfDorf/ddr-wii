// SPDX-License-Identifier: GPL-3.0-or-later
//
// ddr-wii -- a StepMania-style rhythm game for Wii homebrew.
// Copyright (C) 2026 ddr-wii contributors
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version. See the LICENSE file in the repository root.
//
// Reading and writing the small text files the game keeps on the SD card
// (simfiles, Pack.ini, config.ini, scores.txt, cache.txt). Original to
// ddr-wii.

#ifndef DDR_CORE_UTIL_TEXTFILE_H
#define DDR_CORE_UTIL_TEXTFILE_H

#include <string>

namespace ddr {

/** Read a whole file. Returns false on error. */
bool ReadFileToString(const std::string& path, std::string& out);

/** Writes text to path + ".tmp", then renames it over path. Returns false on
 *  any error; the old file is then left as it was, except when only the final
 *  rename failed (FAT can't rename over a file, so the old one is removed
 *  first). */
bool WriteFileAtomic(const std::string& path, const std::string& text);

/** s with tabs and line breaks turned into spaces, for one field of a
 *  tab-separated line (scores.txt, cache.txt). */
std::string CleanField(std::string s);

}  // namespace ddr

#endif
