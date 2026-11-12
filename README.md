# CubeMusic

A GameCube homebrew player for the music on your own original game discs. It reads the disc in the drive, finds the game's music, lets you play it and back the tracks up to an SD card to play later without the disc.

## Features

- **Play from disc.** Tracks are found using a database of known music files per game from a database of VGM rips that were iterated over to produce a database of music files per game.
- **Disc search.** For games the database doesn't know, CubeMusic can search the disc. It opens every file vgmstream might play and keeps the ones long enough to be music. The results are remembered on the SD card.
- **Back up to SD.** Ticked tracks are copied to `/cubemusic/backup/<game id>/`, together with any companion files they need, keeping their disc paths.
- **Saved tracks.** You can browse, play, rename, hide and delete backed-up tracks without the disc.
- **Formats.** Decoding uses [vgmstream](https://github.com/vgmstream/vgmstream). Only the parsers listed in `vgm_formats.txt` are built in, including DSP, ADX, HPS, PDT, RSF and many others. Banks with several streams (subsongs) are listed per stream.
- **Names and info.** Track names, hidden tracks and search results are kept in `/cubemusic/names/<game id>.json`, track lengths in `/cubemusic/cache/`, and settings in `/cubemusic/settings.json`.

## Building

You need [devkitPro](https://devkitpro.org/wiki/Getting_Started) with devkitPPC and the [libogc2](https://github.com/extremscorner/libogc2) package repository set up. Install the packages:

```
dkp-pacman -S gamecube-tools-git libogc2 libogc2-libdvm libogc2-libansnd ppc-jansson ppc-freetype
```

`ppc-freetype` brings in libpng, zlib, bzip2 and brotli. Then:

```
make
```

vgmstream is statically linked into the DOL. The Makefile generates a cut-down copy of vgmstream's format table from `vgm_formats.txt`. To add a format, add its parser's name there.


## Running

Download `cubemusic-<version>.zip` from the releases page and unzip it to the root of your SD card, or Install it via [DolphinDepot](https://dolphindepot.org). 

Load via from Swiss, or any compatible loader.


## Music database

CubeMusic comes with a database of music entries for games it knows, but they only contain the raw filenames. Eventually I will start to ship a .json database alongside releases with renamed tracks, but I'm hoping some of the community can help with this as it's a big undertaking.

Track names live in `db/`, one file per game, named after the game ID with the region letter replaced by `_` (so `db/GM8_01.json` covers every region of Metroid Prime for example). Releases put them in `/cubemusic/names/` on the SD card. Each file lists tracks by their path on the disc (the end of the path is enough) and the name to show:

```json
{ "title": "Metroid Prime",
  "tracks": [
    { "path": "frontend_1.rsf", "name": "Metroid Prime Title" },
    { "path": "rui_samusL.dsp", "name": "Chozo Ruins" } ] }
```

To start a names file for a game, choose **Export a names file** from the Options menu (X) in its track list, from the disc or from the saved tracks. Every track is written with its current name to `/cubemusic/export/<GAM>_<MK>.json`. Each entry also gets a `length` field, and `loops` for tracks that loop, to help tell tracks apart; CubeMusic ignores both when loading the file. Fill in the names on a computer and put the file in `/cubemusic/names/`.

For a stereo pair split into L and R files, name the L file. A track can also be hidden with `"hidden": true`. Names you change on the console are saved to a separate file for your region (`GM8E01.json`), so a newer release never overwrites them. When that file exists it is used instead of the one from `db/`.

## Licences

| Component | Use | Licence |
|---|---|---|
| [vgmstream](https://github.com/vgmstream/vgmstream) (`vgmstream/`) | audio decoding, compiled in, lightly patched (see `vgmstream/VERSION`) | ISC-style (`vgmstream/COPYING`) |
| [M PLUS Rounded 1c](https://github.com/coz-m/MPLUS_FONTS) (`data/mplus.ttf`) | UI font, embedded | SIL Open Font License 1.1 (`licenses/OFL-MPLUSRounded1c.txt`) |
| [FreeType](https://freetype.org) | font rendering | FreeType License (FTL) |
| [Jansson](https://github.com/akheron/jansson) | JSON files on SD | MIT |
| [libpng](http://www.libpng.org), [zlib](https://zlib.net), [bzip2](https://sourceware.org/bzip2/), [Brotli](https://github.com/google/brotli) | FreeType dependencies | libpng / zlib / bzip2 (BSD-style) / MIT |
| [libogc2](https://github.com/extremscorner/libogc2) | system library | zlib-style, plus a BSD-style notice for its RTEMS-derived parts (`rtems_license.txt`) |
| [libdvm](https://github.com/extremscorner/libdvm) (its `libfat`) | SD card file system | Zope Public License 2.1 |
| [libansnd](https://github.com/extremscorner/libansnd) | audio output | zlib-style |
| newlib (devkitPPC's C library) | C runtime | various permissive licences |
| GCMM exFAT / Flashcube code (`source/dolload.c`, `source/sfxsynth.c`) | DOL loader, menu sound synth | MIT |
| `source/gcm_fst.c` | disc file table | MIT, © 2016 emu_kidid |

Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved.

The licence texts for these libraries come with devkitPro:
- the portlibs (FreeType, Jansson, libpng, zlib, bzip2, Brotli) are in devkitPro's `portlibs/ppc/licenses/`
- libogc2 is in devkitPro's `libogc2/*_license.txt`
- libdvm and libansnd are in devkitPro's `libogc2/gamecube/share/licenses/`


CubeMusic doesn't include any game music or Nintendo code. It plays and backs up music from discs you own. `source/meta.c` contains only file names.

This project has been developed with assistance from an LLM, and the code has been tested/reviewed by me (emu_kidid). This project sat dead for many years (I started it in 2016!) and wouldn't have seen the end of the tunnel without the assistance.
