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
// Text file helpers. Original to ddr-wii.

#include "TextFile.h"

#include <cstdio>

namespace ddr {

bool ReadFileToString(const std::string& path, std::string& out) {
  out.clear();
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  char buf[16384];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
  bool ok = !std::ferror(f);
  std::fclose(f);
  return ok;
}

bool WriteFileAtomic(const std::string& path, const std::string& text) {
  const std::string tmp = path + ".tmp";
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

std::string CleanField(std::string s) {
  for (char& c : s)
    if (c == '\t' || c == '\r' || c == '\n') c = ' ';
  return s;
}

}  // namespace ddr
