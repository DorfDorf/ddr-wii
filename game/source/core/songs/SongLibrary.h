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
// Song library (docs/06-technical-design.md section 7): scans
// <root>/<pack>/<song>/ for simfiles, reads each pack's Pack.ini and keeps a
// short summary of every song, sorted by pack then title. Original to
// ddr-wii. Portable: only <dirent.h> and <sys/stat.h>, which newlib (libfat)
// and the PC both have.

#ifndef DDR_CORE_SONGS_SONGLIBRARY_H
#define DDR_CORE_SONGS_SONGLIBRARY_H

#include <map>
#include <string>
#include <vector>

#include "../chart/Chart.h"
#include "../chart/PackIni.h"

namespace ddr {

/** One dance-single chart of a song. */
struct ChartInfo {
  int index = 0;  // into Song::charts after loading the simfile again
  Difficulty difficulty = Difficulty::Beginner;
  int meter = 1;
  std::string description;
  // Counts for the select screen (as ITG's pane): judgable notes only.
  int steps = 0;   // rows with a tap, hold or roll head
  int jumps = 0;   // rows with two or more of them
  int holds = 0, rolls = 0, mines = 0;
};

struct SongEntry {
  std::string dir;      // "<root>/<pack>/<song>/", with the trailing '/'
  std::string folder;   // "<song>"
  std::string simfile;  // file name in dir (.ssc before .sm)
  std::string music;    // file name in dir; "" if none was found
  std::string banner;   // file name in dir; "" if none
  std::string title, subtitle, artist;
  std::string titleTranslit, subtitleTranslit;
  double sampleStart = 0, sampleLength = 0;
  double bpmMin = 0, bpmMax = 0;  // #DISPLAYBPM if set, else the song's BPM segments
  bool bpmRandom = false;         // #DISPLAYBPM:*
  double lengthSeconds = 0;       // last note (or hold end) of the longest chart
  std::vector<ChartInfo> charts;  // dance-single only, sorted by difficulty

  std::string SimfilePath() const { return dir + simfile; }
  std::string MusicPath() const { return music.empty() ? std::string() : dir + music; }
};

struct PackEntry {
  std::string dir;  // "<root>/<pack>/"
  PackInfo info;    // syncOffset goes to Song::SetGroupOffset() at load time
  std::string banner;  // file name in dir (Pack.ini Banner, else guessed); "" if none
  std::vector<SongEntry> songs;
};

struct SongLibrary {
  std::vector<PackEntry> packs;  // packs with at least one playable song
  /** Songs that were skipped and why ("<pack>/<song>: no simfile", ...). */
  std::vector<std::string> warnings;

  size_t SongCount() const;
};

/**
 * The scan's results per song folder, kept in sd:/ddrwii/cache.txt so a boot
 * only parses the simfiles that changed. An item is reused when its
 * signature matches: a hash of the folder's file and subfolder names and the
 * simfile's size and modification time. Songs that were skipped are cached
 * too, with their warning. Items for folders that are gone are dropped.
 */
struct SongCache {
  static constexpr int kVersion = 1;  // bump when SongEntry/ChartInfo change
  struct Item {
    std::string sig;
    bool ok = false;      // false: skipped, see warning
    SongEntry song;       // ok only
    std::string warning;  // !ok only
  };
  std::map<std::string, Item> items;  // by SongEntry::dir
  int hits = 0, misses = 0;           // from the last scan
  bool dirty = false;                 // changed by the last scan: save it

  /** Replaces items with the text of a cache.txt. A different version or a
   *  bad line drops what can't be read; those songs are parsed again. */
  void Parse(const std::string& text);
  std::string Write() const;
};

bool LoadSongCache(const std::string& path, SongCache& out);
bool SaveSongCache(const std::string& path, const SongCache& cache);

/**
 * Scan root ("sd:/ddrwii/songs"). A pack is a folder in root, a song is a
 * folder in a pack holding a .ssc or .sm. Files directly in root and folders
 * deeper than <pack>/<song>/ are ignored, as in StepMania. A song is kept if
 * its simfile loads and has at least one dance-single chart with notes.
 * Packs are sorted by Pack.ini SortTitle (else the folder name), songs by
 * transliterated title, subtitle, then folder; all case-insensitive.
 * Returns false only if root can't be opened.
 */
bool ScanSongLibrary(const std::string& root, SongLibrary& out,
                     DefaultSyncOffset def = DefaultSyncOffset::Null,
                     SongCache* cache = nullptr);

/** The entries of one folder, split into subfolders and files, sorted, without
 *  "." and "..". Returns false if the folder can't be opened. */
bool ListFolder(const std::string& dir, std::vector<std::string>& folders,
                std::vector<std::string>& files);

}  // namespace ddr

#endif
