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

#include "../chart/SmLoader.h"  // ReadFileToString
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
  for (int p = 0; p < Config::kPorts; p++) {
    char section[8];
    std::snprintf(section, sizeof section, "Port%d", p + 1);
    if (GetNumber(ini, section, "InputOffset", d)) out.ports[p].inputOffsetS = ClampOffset(d / 1000.0);
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
  for (int p = 0; p < Config::kPorts; p++) {
    std::snprintf(buf, sizeof buf, "\n[Port%d]\nInputOffset=%.1f\n", p + 1, c.ports[p].inputOffsetS * 1000.0);
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
  const std::string tmp = path + ".tmp";
  const std::string text = WriteConfigText(c);
  FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) return false;
  const bool wrote = std::fwrite(text.data(), 1, text.size(), f) == text.size();
  const bool closed = std::fclose(f) == 0;
  if (!wrote || !closed) {
    std::remove(tmp.c_str());
    return false;
  }
  if (std::rename(tmp.c_str(), path.c_str()) == 0) return true;
  // FAT (libfat) and Windows don't rename over an existing file.
  std::remove(path.c_str());
  return std::rename(tmp.c_str(), path.c_str()) == 0;
}

}  // namespace ddr
