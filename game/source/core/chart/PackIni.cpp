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
// Derived from ITGmania v1.3.0 src/Group.cpp (ITGmania contributors, GPLv3,
// no separate notice) and src/IniFile.cpp ("(c) 2001-2004 Adam Clauss,
// Chris Danford", same MIT-style permission notice as reproduced in
// MsdFile.cpp) from https://github.com/itgmania/itgmania.
//
// Changes from the original:
//  - No RageFile/FILEMAN/PREFSMAN/SONGMAN/Lua: the caller passes the Pack.ini
//    text (or a path) and the DefaultSyncOffset preference.
//  - IniFile keeps only the line parser, storing sections in a std::map
//    instead of an XNode tree.
//  - Group's profile-path variant is dropped.

#include "PackIni.h"

#include <utility>
#include <vector>

#include "SmLoader.h"  // ReadFileToString
#include "StringUtil.h"

namespace ddr {

void IniFile::ReadFromString(const std::string& text) {
  std::map<std::string, std::string>* keychild = nullptr;
  size_t pos = 0;
  while (pos < text.size()) {
    std::string line;
    // Read lines until we reach a line that doesn't end in a backslash
    for (;;) {
      if (pos >= text.size()) break;
      size_t nl = text.find('\n', pos);
      std::string s = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
      pos = (nl == std::string::npos) ? text.size() : nl + 1;
      if (!s.empty() && s.back() == '\r') s.pop_back();
      // utf8_remove_bom
      if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
          static_cast<unsigned char>(s[1]) == 0xBB &&
          static_cast<unsigned char>(s[2]) == 0xBF)
        s.erase(0, 3);

      line += s;

      if (line.empty() || line[line.size() - 1] != '\\') {
        break;
      }
      line.erase(line.end() - 1);
    }

    if (line.empty()) {
      continue;
    }
    bool keyvalue = false;
    switch (line[0]) {
      case ';':
      case '#':
        continue;  // comment
      case '/':
      case '-':
        if (line.size() > 1 && line[0] == line[1]) {
          continue;  // comment (Lua or C++ style)
        }
        keyvalue = true;
        break;
      case '[':
        if (line[line.size() - 1] == ']') {
          // New section.
          keychild = &m_sections[line.substr(1, line.size() - 2)];
          break;
        }
        keyvalue = true;
        break;
      default:
        keyvalue = true;
        break;
    }
    if (keyvalue && keychild != nullptr) {
      // New value.
      size_t iEqualIndex = line.find('=');
      if (iEqualIndex != std::string::npos) {
        std::string valuename = line.substr(0, iEqualIndex);
        std::string value = line.substr(iEqualIndex + 1);
        Trim(valuename);
        if (!valuename.empty()) {
          (*keychild)[valuename] = value;
        }
      }
    }
  }
}

bool IniFile::GetValue(const std::string& section, const std::string& key,
                       std::string& out) const {
  auto s = m_sections.find(section);
  if (s == m_sections.end()) return false;
  auto k = s->second.find(key);
  if (k == s->second.end()) return false;
  out = k->second;
  return true;
}

PackInfo ParsePackIni(const std::string& groupName, const std::string* packIniText,
                      DefaultSyncOffset def) {
  PackInfo g;
  g.groupName = groupName;
  g.displayTitle = groupName;
  g.sortTitle = groupName;
  g.translitTitle = groupName;
  g.version = 2;  // INI_VERSION
  // The DefaultSyncOffset preference corresponds to what offset the user
  // would like to assume as a "default" for packs without a Pack.ini.
  //
  // If it is set to ITG, we want to remove the 9ms ITG bias for all packs
  // without a Pack.ini.
  g.syncOffset = (def == DefaultSyncOffset::Null) ? 0.0 : ITG_SYNC_OFFSET;

  if (packIniText == nullptr) return g;

  IniFile ini;
  ini.ReadFromString(*packIniText);

  std::string sVersion;
  ini.GetValue("Group", "Version", sVersion);
  Trim(sVersion);

  // Only read the Pack.ini if the version is set.
  // Otherwise use default Pack.ini values.
  if (sVersion.empty()) return g;

  g.hasPackIni = true;
  g.version = StringToInt(sVersion);

  std::vector<std::pair<std::string, std::string*>> vPackfields = {
      {"DisplayTitle", &g.displayTitle},   {"SortTitle", &g.sortTitle},
      {"TranslitTitle", &g.translitTitle}, {"Series", &g.series},
      {"Banner", &g.banner},               {"SeriesBanner", &g.seriesBanner}};

  for (auto& kv : vPackfields) {
    const std::string& key = kv.first;
    std::string& value = *kv.second;
    ini.GetValue("Group", key, value);
    Trim(value);

    if (value.empty()) {
      if (key == "DisplayTitle" || key == "SortTitle") {
        value = g.groupName;
      } else if (key == "TranslitTitle") {
        value = g.displayTitle;
      }
    }
  }

  std::string sValue;
  ini.GetValue("Group", "SyncOffset", sValue);
  Trim(sValue);
  // If the Pack.ini says SyncOffset=ITG (regardless of the default), always
  // remove the 9ms ITG bias. SyncOffset=NULL means it is already NULL synced.
  if (!sValue.empty()) {
    if (sValue == "NULL") {
      g.syncOffset = 0.0;
    } else if (sValue == "ITG") {
      g.syncOffset = ITG_SYNC_OFFSET;
    }
    // Anything else: invalid, keep the default.
  }

  std::string sYear;
  if (ini.GetValue("Group", "Year", sYear)) g.year = StringToInt(sYear);
  return g;
}

PackInfo LoadPackInfo(const std::string& packDir, const std::string& groupName,
                      DefaultSyncOffset def) {
  std::string text;
  std::string path = packDir;
  if (!path.empty() && path.back() != '/' && path.back() != '\\') path += '/';
  path += "Pack.ini";
  if (!ReadFileToString(path, text)) return ParsePackIni(groupName, nullptr, def);
  return ParsePackIni(groupName, &text, def);
}

}  // namespace ddr
