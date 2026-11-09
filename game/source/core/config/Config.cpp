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
// config.ini reader and writer. Original to ddr-wii.

#include "Config.h"

#include <cstdio>
#include <cstdlib>

#include "../util/TextFile.h"
#include "../chart/StringUtil.h"

namespace ddr {

namespace {

bool GetNumber(const IniFile& ini, const char* section, const char* key, double& out) {
  std::string v;
  if (!ini.GetValue(section, key, v)) return false;
  Trim(v);
  if (v.empty()) return false;
  char* end = nullptr;
  const double d = std::strtod(v.c_str(), &end);
  if (end == v.c_str() || *end != '\0') return false;
  out = d;
  return true;
}

double ClampOffset(double s) {
  if (s > Config::kMaxOffsetS) return Config::kMaxOffsetS;
  if (s < -Config::kMaxOffsetS) return -Config::kMaxOffsetS;
  return s;
}

}  // namespace

const char* LifeBarToString(LifeBar b) {
  switch (b) {
    case LifeBar::On: return "On";
    case LifeBar::NoFail: return "NoFail";
    default: return "Off";
  }
}

void ParseConfig(const std::string& text, Config& out) {
  IniFile ini;
  ini.ReadFromString(text);
  double d;
  if (GetNumber(ini, "Options", "GlobalOffset", d)) out.globalOffsetS = ClampOffset(d / 1000.0);
  std::string s;
  if (ini.GetValue("Options", "DefaultSyncOffset", s)) {
    Trim(s);
    if (EqualsNoCase(s, "ITG")) out.defaultSyncOffset = DefaultSyncOffset::ITG;
    if (EqualsNoCase(s, "NULL")) out.defaultSyncOffset = DefaultSyncOffset::Null;
  }
  if (GetNumber(ini, "Options", "InputDebounceMs", d) && d >= 0 && d <= 50)
    out.inputDebounceMs = (int)(d + 0.5);
  // The old PlayMode key (gone since the song settings popup): Competitive
  // turns the life bar on for ports that have no LifeBar of their own yet.
  bool competitive = false;
  if (ini.GetValue("Options", "PlayMode", s)) {
    Trim(s);
    competitive = EqualsNoCase(s, "Competitive");
  }
  for (int p = 0; p < Config::kPorts; p++) {
    char section[8];
    std::snprintf(section, sizeof section, "Port%d", p + 1);
    if (ini.GetValue(section, "LifeBar", s)) {
      Trim(s);
      if (EqualsNoCase(s, "Off")) out.ports[p].lifeBar = LifeBar::Off;
      if (EqualsNoCase(s, "On")) out.ports[p].lifeBar = LifeBar::On;
      if (EqualsNoCase(s, "NoFail")) out.ports[p].lifeBar = LifeBar::NoFail;
    } else if (competitive) {
      out.ports[p].lifeBar = LifeBar::On;
    }
    if (GetNumber(ini, section, "InputOffset", d)) out.ports[p].inputOffsetS = ClampOffset(d / 1000.0);
    if (GetNumber(ini, section, "ScrollSpeed", d) && d >= 0.25 && d <= 8)
      out.ports[p].scrollSpeed = (int)(d * 4 + 0.5) / 4.0;   // steps of 0.25
    if (ini.GetValue(section, "Name", s)) {
      Trim(s);
      MakeUpper(s);
      if (s.size() == 3 && s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") == std::string::npos)
        out.ports[p].name = s;
    }
  }
}

std::string WriteConfigText(const Config& c) {
  std::string t;
  char buf[96];
  t += "; ddr-wii settings, written by the game (edit with the game off).\n";
  t += "; Offsets are in milliseconds. GlobalOffset is added to the song time:\n";
  t += "; lower it if your steps are judged late. Set it with the calibration screen.\n";
  t += "[Options]\n";
  std::snprintf(buf, sizeof buf, "GlobalOffset=%.1f\n", c.globalOffsetS * 1000.0);
  t += buf;
  t += "; NULL or ITG: the sync of packs without a Pack.ini\n";
  t += c.defaultSyncOffset == DefaultSyncOffset::ITG ? "DefaultSyncOffset=ITG\n" : "DefaultSyncOffset=NULL\n";
  std::snprintf(buf, sizeof buf, "InputDebounceMs=%d\n", c.inputDebounceMs);
  t += buf;
  t += "\n; Per pad port. LifeBar: Off, On (ITG's bar and failing) or NoFail.\n";
  t += "; Name: the last name saved on Results from this pad (three letters).\n";
  for (int p = 0; p < Config::kPorts; p++) {
    std::snprintf(buf, sizeof buf, "\n[Port%d]\nInputOffset=%.1f\nScrollSpeed=%.2f\nLifeBar=%s\nName=%s\n", p + 1,
                  c.ports[p].inputOffsetS * 1000.0, c.ports[p].scrollSpeed,
                  LifeBarToString(c.ports[p].lifeBar), c.ports[p].name.c_str());
    t += buf;
  }
  return t;
}

bool LoadConfig(const std::string& path, Config& out) {
  std::string text;
  if (!ReadFileToString(path, text)) return false;
  ParseConfig(text, out);
  return true;
}

bool SaveConfig(const std::string& path, const Config& c) {
  return WriteFileAtomic(path, WriteConfigText(c));
}

}  // namespace ddr
