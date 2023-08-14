# Interstice ProQuake v5.01

This repository contains _Interstice_, an interation upon [_Insomnia_](https://github.com/darkduke606/Insomnia-ProQuake-Engine), part of the bloodline of ProQuake sourceports for the PlayStation Portable handheld.

## Building
Interstice is built using the latest [PSPSDK](https://github.com/pspdev/psptoolchain) available, there is a [Docker](https://hub.docker.com/r/pspdev/pspdev) container available to make deployment easy.

Build with `make`.

`build_release.sh` is a script to automate the build process, as well as place controller bindings and other important files from `assets`. It also downloads the shareware version of Quake from archive.org for easy, legal distribution.

## Compatibility List
| Mod Name | Directory Name | Status (PHAT) | Status (SLIM) |
|----------|:--------------:|:-------------:|:--------------|
| Quake | `id1` | Good | Good |
| Scourge of Armagon | `hypnotic` | Good | Good |
| Dissolution of Eternity | `rogue` | Decent | Good |
| In The Shadows | `shadows` | Unplayable | Unplayable |
| UltraQuake | `ultraquake` | Unplayable | Unplayable |
| I WANT TOAST | `toast` | Good | Good |
| Abandon | `abandon` | Good | Good |
| Dimension of the Past | `dopa` | Good | Good |
| LibreQuake | `lq1` | Decent | Decent |
| Kickflip Quake | `skate` | Good | Good |
| Quake Xmas Jam 2018 | `xmasjam2018` | Unplayable | Unplayable |
| NAVY SEALS | `navyseal` | Unplayable | Unplayable |
| Beyond Belief | `bbelief` | Good | Good |
| Rubicon 2 | `rubicon2` | Unplayable | Decent |
| The Punishment Due | `pun` | Unplayable | Unplayable |
| Terra | `terra` | Good | Good |
| Honey | `honey` | Unplayable | Decent |
| Air Quake I | `airquake` | Unplayable | Good |
| Quake I Arena | `q1a` | Good | Good |
| Slide | `slide` | Unplayable | Unplayable |
| X-MEN: Ravages of Apocalypse | `xmen` | Unplayable | Unplayable |
| Abyss of Pandemonium - The Final Mission | `aop` | Decent | Good |
| SUPERHOT Quake | `shquake` | Good | Good |

## CD Audio Support

Interstice uses a hardware accelerated `.mp3` decoder for CD Audio support, this means other formats like `.flac` and `.ogg` are unsupported. It uses the widely-adopted standard for path and file name, that standard being `<GAMEDIR>/music/track01.mp3`. It will fall back to the `id1` directory to try and load a track there if one is not present in the loaded game directory. It is recommended to lower the rate of your music tracks to 22kHz, but the standard 44.1kHz sourced directly from Quake CDs will play without issue. Interstice fixes the bug with Mission Pack 1's `HIPDEMO.DEM` referencing a track `98` instead of `02`.

## Changelog

### 5.01
* Add more compatibility list entries:
  - Kickflip Quake
  - Quake Xmas Jam 2018
  - NAVY SEALS
  - Beyond Belief
  - Rubicon 2
  - The Punishment Due
  - Terra
  - Honey
  - AirQuake I
  - Quake I Arena
  - Slide
  - X-MEN: Ravages of Apocalypse
  - Abyss of Pandemonium - The Final Mission
* The Quake hunk being used completely no longer suggests applying `-mem 64` to `quake.cmdline`.
* Textures greater than 800px in width or height now forcefully resampled (fixes `progs/skater.mdl` in Kickflip Quake)
* Add new splash texts on the mod interface:
  - THE SHAMBLER HAS FUR
  - THE SHAMBLER HAS NO FUR
  - 50% compatibility on a good day!
  - technically i'm saying anything

### 5.00 (First Release)
* Several optmizations inherited from [NZ:P Engine (dQuakePlus)](https://github.com/nzp-team/dquakeplus), [ADQuake](https://github.com/st1x51/adquake), and [FTEQW](https://github.com/fte-team/fteqw).
* A unified binary: No separate EBOOT.PBP files for low and high memory systems, with no compromises made to high memory system advantage.
* Restructured source to remove depreciated features, and adopt the `gu_` code standard (as opposed to `video_hardware`).
* A new navigation interface with support for mod detection (via directory name) with information on compability based on your device.