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
// config.ini (docs/06-technical-design.md section 7): the settings the game
// keeps on the SD card. Read with IniFile (chart/PackIni.h); written whole,
// to a temporary file that is then renamed over the old one. Original to
// ddr-wii.

#ifndef DDR_CORE_CONFIG_CONFIG_H
#define DDR_CORE_CONFIG_CONFIG_H

#include <string>

#include "../chart/PackIni.h"

namespace ddr {

struct PortConfig {
  /** Seconds added to this port's step times (a mat that reads later than
   *  another). */
  double inputOffsetS = 0;
};

struct Config {
  static constexpr int kPorts = 4;
  static constexpr double kMaxOffsetS = 1.0;  // offsets are clamped to +-1 s

  /** Chart time = song time + this (doc 06 section 3.2). Stored in ms. */
  double globalOffsetS = 0;
  /** For packs without a Pack.ini. */
  DefaultSyncOffset defaultSyncOffset = DefaultSyncOffset::Null;
  /** Pad debounce window (InputFilter); 0-50 ms. */
  int inputDebounceMs = 20;
  PortConfig ports[kPorts];
};

/** Reads config.ini text into out, starting from out's current values: keys
 *  that are missing or invalid keep them. */
void ParseConfig(const std::string& text, Config& out);
/** The text of a config.ini holding c. */
std::string WriteConfigText(const Config& c);

/** Reads path into out. Returns false (out unchanged) if there is no file. */
bool LoadConfig(const std::string& path, Config& out);
/** Writes path + ".tmp", then renames it over path. Returns false on any
 *  error; the old file is then left as it was, except when only the final
 *  rename failed (FAT can't rename over a file, so the old one is removed
 *  first). */
bool SaveConfig(const std::string& path, const Config& c);

}  // namespace ddr

#endif
