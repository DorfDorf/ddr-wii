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
// Song library scan. Original to ddr-wii; the layout rules follow ITGmania's
// SongManager (groups are folders in Songs/, songs are folders in a group),
// re-implemented, not ported.

#include "SongLibrary.h"

#include <dirent.h>
#include <cstdio>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <tuple>

#include "../chart/SmLoader.h"
#include "../chart/StringUtil.h"

namespace ddr {

namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

bool HasExt(const std::string& name, const char* ext) {
  const std::string e = ext;
  return name.size() > e.size() && Lower(name.substr(name.size() - e.size())) == e;
}

bool IsAudio(const std::string& f) {
  return HasExt(f, ".ogg") || HasExt(f, ".mp3") || HasExt(f, ".wav");
}

bool IsImage(const std::string& f) {
  return HasExt(f, ".png") || HasExt(f, ".jpg") || HasExt(f, ".jpeg");
}

/** The file in files that equals name, ignoring case ("" if none). Simfiles
 *  often get the case of their #MUSIC wrong, and FAT doesn't care. */
std::string FindNoCase(const std::vector<std::string>& files, const std::string& name) {
  if (name.empty()) return std::string();
  const std::string want = Lower(name);
  for (const std::string& f : files)
    if (Lower(f) == want) return f;
  return std::string();
}

std::string PickMusic(const Song& song, const std::vector<std::string>& files) {
  std::string m = FindNoCase(files, song.music);
  if (!m.empty() && IsAudio(m)) return m;
  for (const std::string& f : files)
    if (IsAudio(f)) return f;
  return std::string();
}

std::string PickBanner(const std::string& tag, const std::vector<std::string>& files) {
  std::string b = FindNoCase(files, tag);
  if (!b.empty() && IsImage(b)) return b;
  // No (valid) #BANNER: a file named like "bn.png", "song-bn.png" or "banner.png".
  for (const std::string& f : files) {
    if (!IsImage(f)) continue;
    std::string stem = Lower(f.substr(0, f.rfind('.')));
    if (stem.find("banner") != std::string::npos) return f;
    if (stem.size() >= 2 && stem.compare(stem.size() - 2, 2, "bn") == 0) return f;
  }
  return std::string();
}

void CountNotes(const Chart& c, ChartInfo& ci) {
  int lastRow = -1, onRow = 0;
  auto endRow = [&]() {
    if (onRow > 0) ci.steps++;
    if (onRow > 1) ci.jumps++;
  };
  for (const Note& n : c.notes) {
    if (!n.judgable) continue;
    if (n.type == NoteType::Mine) { ci.mines++; continue; }
    if (n.type != NoteType::Tap && !n.IsHold()) continue;
    if (n.type == NoteType::HoldHead) ci.holds++;
    if (n.type == NoteType::RollHead) ci.rolls++;
    if (n.row != lastRow) { endRow(); onRow = 0; lastRow = n.row; }
    onRow++;
  }
  endRow();
}

void BpmRange(const Song& song, SongEntry& s) {
  if (song.displayBpmRandom) { s.bpmRandom = true; return; }
  if (song.displayBpmSpecified) {
    s.bpmMin = song.displayBpmMin;
    s.bpmMax = song.displayBpmMax;
    return;
  }
  bool first = true;
  for (const TimingSegment& seg : song.timing.GetTimingSegments(SEGMENT_BPM)) {
    const double bpm = seg.value * 60.0;
    if (bpm <= 0) continue;
    if (first || bpm < s.bpmMin) s.bpmMin = bpm;
    if (first || bpm > s.bpmMax) s.bpmMax = bpm;
    first = false;
  }
}

std::string SortKey(const std::string& s) { return Lower(Trimmed(s)); }

std::string WithSlash(std::string s) {
  if (!s.empty() && s.back() != '/') s += '/';
  return s;
}

}  // namespace

size_t SongLibrary::SongCount() const {
  size_t n = 0;
  for (const PackEntry& p : packs) n += p.songs.size();
  return n;
}

bool ListFolder(const std::string& dir, std::vector<std::string>& folders,
                std::vector<std::string>& files) {
  folders.clear();
  files.clear();
  DIR* d = opendir(dir.c_str());
  if (d == nullptr) return false;
  const std::string base = WithSlash(dir);
  while (struct dirent* e = readdir(d)) {
    const std::string name = e->d_name;
    if (name.empty() || name == "." || name == "..") continue;
    bool isDir = false;
#ifdef DT_DIR
    if (e->d_type == DT_DIR) {
      isDir = true;
    } else if (e->d_type == DT_UNKNOWN) {
      struct stat st;
      isDir = stat((base + name).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }
#else
    struct stat st;
    isDir = stat((base + name).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
    (isDir ? folders : files).push_back(name);
  }
  closedir(d);
  std::sort(folders.begin(), folders.end());
  std::sort(files.begin(), files.end());
  return true;
}

namespace {

// Scans one song folder whose listing is files. Returns false with a warning
// (without the "<pack>/<song>: " prefix) when the song can't be played.
bool ScanSong(const std::vector<std::string>& files, SongEntry& s, std::string& warning) {
  s.simfile = PickSimfile(files);
  if (s.simfile.empty()) {
    warning = "no .ssc or .sm";
    return false;
  }
  Song song;
  if (!LoadSongFile(s.SimfilePath(), song)) {
    warning = s.simfile + " has no charts";
    return false;
  }
  for (size_t i = 0; i < song.charts.size(); i++) {
    const Chart& c = song.charts[i];
    if (c.stepsType != "dance-single" || c.notes.empty()) continue;
    ChartInfo ci;
    ci.index = (int)i;
    ci.difficulty = c.difficulty;
    ci.meter = c.meter;
    ci.description = c.description;
    CountNotes(c, ci);
    for (const Note& n : c.notes) s.lengthSeconds = std::max(s.lengthSeconds, n.IsHold() ? n.endTime : n.time);
    s.charts.push_back(ci);
  }
  if (s.charts.empty()) {
    warning = "no dance-single charts";
    return false;
  }
  std::stable_sort(s.charts.begin(), s.charts.end(), [](const ChartInfo& a, const ChartInfo& b) {
    return std::make_tuple((int)a.difficulty, a.meter) < std::make_tuple((int)b.difficulty, b.meter);
  });
  s.music = PickMusic(song, files);
  if (s.music.empty()) {
    warning = "no audio file";
    return false;
  }
  s.banner = PickBanner(song.banner, files);
  s.title = song.title.empty() ? s.folder : song.title;
  s.subtitle = song.subtitle;
  s.artist = song.artist;
  s.titleTranslit = song.titleTranslit;
  s.subtitleTranslit = song.subtitleTranslit;
  s.sampleStart = song.sampleStart;
  s.sampleLength = song.sampleLength;
  BpmRange(song, s);
  return true;
}

// FNV-1a over the folder listing and the simfile's size and time, as hex.
std::string Signature(const std::string& dir, const std::vector<std::string>& folders,
                      const std::vector<std::string>& files) {
  unsigned long long h = 1469598103934665603ULL;
  auto add = [&h](const std::string& s) {
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ULL;
    h = (h ^ 0xFF) * 1099511628211ULL;   // separator
  };
  for (const std::string& f : folders) add("/" + f);
  for (const std::string& f : files) add(f);
  const std::string sim = PickSimfile(files);
  struct stat st;
  if (!sim.empty() && stat((dir + sim).c_str(), &st) == 0)
    add(std::to_string((long long)st.st_size) + ":" + std::to_string((long long)st.st_mtime));
  char buf[20];
  std::snprintf(buf, sizeof buf, "%016llx", h);
  return buf;
}

}  // namespace

bool ScanSongLibrary(const std::string& rootIn, SongLibrary& out, DefaultSyncOffset def,
                     SongCache* cache) {
  out = SongLibrary();
  const std::string root = WithSlash(rootIn);
  std::vector<std::string> packFolders, rootFiles;
  if (!ListFolder(root, packFolders, rootFiles)) return false;

  std::vector<std::string> songFolders, packFiles, folders, files;
  std::map<std::string, SongCache::Item> seen;   // the cache after this scan
  if (cache) cache->hits = cache->misses = 0;
  for (const std::string& packName : packFolders) {
    PackEntry pack;
    pack.dir = root + packName + "/";
    if (!ListFolder(pack.dir, songFolders, packFiles)) continue;
    // Pack.ini, whatever its case (LoadPackInfo reads it by path).
    const std::string ini = FindNoCase(packFiles, "Pack.ini");
    std::string iniText;
    if (!ini.empty() && ReadFileToString(pack.dir + ini, iniText))
      pack.info = ParsePackIni(packName, &iniText, def);
    else
      pack.info = ParsePackIni(packName, nullptr, def);
    pack.banner = PickBanner(pack.info.banner, packFiles);

    for (const std::string& songName : songFolders) {
      const std::string where = packName + "/" + songName;
      SongEntry s;
      s.dir = pack.dir + songName + "/";
      s.folder = songName;
      if (!ListFolder(s.dir, folders, files)) continue;
      SongCache::Item item;
      if (cache) {
        item.sig = Signature(s.dir, folders, files);
        auto it = cache->items.find(s.dir);
        if (it != cache->items.end() && it->second.sig == item.sig) {
          cache->hits++;
          item = it->second;
        } else {
          cache->misses++;
          cache->dirty = true;
          item.ok = ScanSong(files, s, item.warning);
          if (item.ok) item.song = s;
        }
      } else {
        item.ok = ScanSong(files, s, item.warning);
        if (item.ok) item.song = s;
      }
      if (item.ok) {
        pack.songs.push_back(item.song);
        pack.songs.back().dir = s.dir;   // the cache key, so already equal
      } else {
        out.warnings.push_back(where + ": " + item.warning);
      }
      if (cache) seen[s.dir] = std::move(item);
    }
    if (pack.songs.empty()) continue;

    std::sort(pack.songs.begin(), pack.songs.end(), [](const SongEntry& a, const SongEntry& b) {
      auto key = [](const SongEntry& s) {
        return std::make_tuple(SortKey(s.titleTranslit.empty() ? s.title : s.titleTranslit),
                               SortKey(s.subtitleTranslit.empty() ? s.subtitle : s.subtitleTranslit),
                               Lower(s.folder));
      };
      return key(a) < key(b);
    });
    out.packs.push_back(std::move(pack));
  }
  std::sort(out.packs.begin(), out.packs.end(), [](const PackEntry& a, const PackEntry& b) {
    return std::make_tuple(SortKey(a.info.sortTitle), Lower(a.info.groupName)) <
           std::make_tuple(SortKey(b.info.sortTitle), Lower(b.info.groupName));
  });
  if (cache) {
    if (seen.size() != cache->items.size()) cache->dirty = true;   // songs were removed
    cache->items = std::move(seen);
  }
  return true;
}

}  // namespace ddr
