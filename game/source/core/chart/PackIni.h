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
// Pack.ini (song group) metadata. Port of ITGmania v1.3.0 src/Group.cpp and
// the line parser of src/IniFile.cpp. See PackIni.cpp for notices/changes.

#ifndef DDR_CORE_CHART_PACKINI_H
#define DDR_CORE_CHART_PACKINI_H

#include <map>
#include <string>

namespace ddr {

/** What to assume for packs without a usable Pack.ini (config.ini
 *  DefaultSyncOffset). */
enum class DefaultSyncOffset { Null, ITG };

/** Seconds added to every song offset of an ITG-synced pack (removes ITG's
 *  9 ms bias). */
constexpr double ITG_SYNC_OFFSET = -0.009;

struct PackInfo {
  std::string groupName;   // folder name
  std::string displayTitle, sortTitle, translitTitle, series;
  std::string banner, seriesBanner;
  int version = 0;
  int year = 0;
  bool hasPackIni = false;
  /** Pass to Song::SetGroupOffset(). */
  double syncOffset = 0.0;
};

/** A minimal INI reader with StepMania's rules: "[Section]" lines,
 *  "key=value" lines (key trimmed, value not), comments starting with ';',
 *  '#', '//' or '--', a trailing '\' joins the next line, UTF-8 BOMs are
 *  removed. */
class IniFile {
 public:
  void ReadFromString(const std::string& text);
  bool GetValue(const std::string& section, const std::string& key,
                std::string& out) const;

 private:
  std::map<std::string, std::map<std::string, std::string>> m_sections;
};

/**
 * Build a pack's info from its folder name and the text of its Pack.ini
 * (pass nullptr if the pack has none). As in ITGmania, the Pack.ini is only
 * used when it has a non-empty [Group] Version; SyncOffset may be "NULL"
 * (0) or "ITG" (-0.009 s); anything else keeps the default.
 */
PackInfo ParsePackIni(const std::string& groupName, const std::string* packIniText,
                      DefaultSyncOffset def = DefaultSyncOffset::Null);

/** Same, reading "<packDir>/Pack.ini" from disk if it exists. */
PackInfo LoadPackInfo(const std::string& packDir, const std::string& groupName,
                      DefaultSyncOffset def = DefaultSyncOffset::Null);

}  // namespace ddr

#endif
