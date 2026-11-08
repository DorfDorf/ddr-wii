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
  std::vector<ChartInfo> charts;  // dance-single only, sorted by difficulty

  std::string SimfilePath() const { return dir + simfile; }
  std::string MusicPath() const { return music.empty() ? std::string() : dir + music; }
};

struct PackEntry {
  std::string dir;  // "<root>/<pack>/"
  PackInfo info;    // syncOffset goes to Song::SetGroupOffset() at load time
  std::vector<SongEntry> songs;
};

struct SongLibrary {
  std::vector<PackEntry> packs;  // packs with at least one playable song
  /** Songs that were skipped and why ("<pack>/<song>: no simfile", ...). */
  std::vector<std::string> warnings;

  size_t SongCount() const;
};

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
                     DefaultSyncOffset def = DefaultSyncOffset::Null);

/** The entries of one folder, split into subfolders and files, sorted, without
 *  "." and "..". Returns false if the folder can't be opened. */
bool ListFolder(const std::string& dir, std::vector<std::string>& folders,
                std::vector<std::string>& files);

}  // namespace ddr

#endif
