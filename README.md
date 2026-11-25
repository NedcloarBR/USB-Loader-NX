<div align="center">

<img src="assets/icon.svg" width="144" height="144" alt="USB Loader NX icon: a game card ending in a USB plug">

# USB Loader NX

Play your Nintendo Switch game dumps straight from a USB drive.<br>
An Atmosphère fork with a library manager in the spirit of USB Loader GX for the Wii.

[![Atmosphère 1.12.2](https://img.shields.io/badge/Atmosph%C3%A8re-1.12.2-2B2D31?labelColor=1E1F22)](https://github.com/Atmosphere-NX/Atmosphere)
![Tested on firmware 19.0.1 and 23.5.0](https://img.shields.io/badge/tested%20on-19.0.1%20%7C%2023.5.0-2B2D31?labelColor=1E1F22)
[![License GPLv2](https://img.shields.io/badge/license-GPLv2-00C26F?labelColor=1E1F22)](LICENSE)

[Features](#features) · [Install](#install) · [Configuration](#configuration) · [Building](#building) · [Architecture](ARCHITECTURE.md) · [Changelog](CHANGELOG.md) · [TODO](TODO.md) · [License](#license) · [Credits](#credits)

</div>

Games are never copied to the SD card or the console: USB Loader NX reads them from the drive while you play. Plug the drive into the dock and your games appear on the HOME menu.

## Features

| | |
|---|---|
| **Formats** | XCI (game card dumps) and NSP (eShop), with their updates, DLC and DLC updates, also NSZ and XCZ compressed in block mode and NSPs that hold several titles (game, update and DLC in one file); games with several programs (collections); from exFAT or FAT32 drives |
| **HOME menu** | Games show up like installed ones, with a badge marking where they come from |
| **eShop content** | Titlekeys come from the console's own `es` service, no `prod.keys` needed |
| **Newer games** | The firmware a game asks for is dropped on registration, like DBI does |
| **Several drives** | At once, and they survive sleep without notifications or a HOME refresh |
| **Mods** | Romfs mods and translations in `atmosphere/contents/<id>/` apply to USB games too, as to installed ones |
| **Homebrew** | What you put in `usbloadernx/usb/` on a USB drive, homebrew read (read-only) at `sdmc:/usbloadernx/usb/<volume serial>/` (the serial Windows shows, like `A1B2-C3D4`), without a USB driver of their own; the option `homebrew_access` turns it off |
| **Hiding** | Hide games while their drive is unplugged, or single games from the Manager |
| **Notifications** | Ultrahand notifications as a title and a short list: a drive found, connected (games, size, where homebrew read it, files not read), removed, unreadable or not answering; in English and Brazilian Portuguese |
| **Manager** | [`USBLoaderNX-Manager.nro`](manager/README.md): games as a shelf, a grid, covers, cases or shelves by folder (art from [GameTDB](https://www.gametdb.com), optional, the only use of the network), launch, details, play time, favorites, search and sorting, show or hide on HOME, drive usage, settings and [themes](manager/themes/README.md) with their own fonts, images, layout and animated backgrounds |
| **Safety** | The USB drive is mounted read-only |

What comes next, from compressed dumps to installing to USB, is in [TODO.md](TODO.md).

> [!NOTE]
> Tested in the dock, on the USB-C port with an OTG adapter and on the Switch Lite. A 2.5" hard drive (WD Elements 1 TB, powered by the console) works; a large FAT32 drive takes longer to read, and the Manager lists it as being read meanwhile. Hard drives with their own supply or on a powered hub have not been tested yet.

## Install

1. Download `usb-loader-nx-<version>.zip` from the releases and copy its contents to the root of the SD card.

   > [!WARNING]
   > The zip holds a complete Atmosphère. Merge it with your existing `atmosphere/` folder instead of replacing it, so sys-patch and your other mods stay in place.

2. Reboot. USB Loader NX is on by default; to turn it off, add this to `atmosphere/config/system_settings.ini`:

   ```ini
   [usbloadernx]
   enable = u8!0x0
   ```

3. Put your dumps in `usbloadernx/games/` on the USB drive and plug it in. Subfolders up to two levels deep are scanned too (for example `games/<game>/` or `games/<group>/<game>/`). Keep each game's update and DLC on the same drive as the game: parts on another drive are not used.

## Configuration

Everything USB Loader NX and its Manager keep is in `usbloadernx/` on the SD card, except `enable` and `debug`, which Atmosphère reads. Versions before 1.0.0 kept them in `atmosphere/config/usbloadernx/` and `config/usbloadernx-manager/`; they are moved on the first start. How each file is versioned is in [FORMATS.md](FORMATS.md).

| File | What it holds |
|---|---|
| `atmosphere/config/system_settings.ini` | `enable`, and `debug` for extra diagnostics in the log. Packs that copy a boot option's file over it at every boot (such as `cfw_system_settings.ini`) are edited in that file; the Manager says which one |
| `usbloadernx/config.ini` | Everyday options, created with the defaults on first boot and edited by the Manager |
| `usbloadernx/templates/config.ini` | Every option of `config.ini`, documented |
| `usbloadernx/manager.ini` | The Manager's own options |
| `usbloadernx/data/` | What the module and the Manager remember: the games' contents (so HOME does not see them as missing while unplugged), hidden games, favorites |
| `usbloadernx/cache/icons/` | The games' icons for the Manager while their drive is unplugged; can be deleted |
| `usbloadernx/cache/art/` | Art downloaded from GameTDB, while Covers from GameTDB is on; can be deleted |
| `usbloadernx/themes/` | Manager themes |
| `usbloadernx/logs/usbloadernx.log` | What USB Loader NX did: drives, games, registration, errors |

## Common problems

| What happens | What to do |
|---|---|
| The Manager says USB Loader NX is off | It names the file the boot option in use reads and offers **Turn on and restart**. Packs such as CNX copy `cfw_system_settings.ini` over `system_settings.ini` at every boot, so a change made only in `system_settings.ini` is undone |
| The Manager says this Atmosphère does not have USB Loader NX, or that it and the Manager do not match | Copy the `SD` folder of one package over the SD card (merging folders, never replacing `atmosphere/` as a whole) and restart |
| A mod or translation does not show up | Most are made for the game's latest update: put the update on the same drive as the game. Holding L while the game starts turns mods off |
| A notification says files on the drive were not read | The Manager (Settings → Diagnostics) and the log (`left out ...`) list each one with the reason: a path longer than 767 bytes, a folder more than two levels inside `games`, more than 4096 entries in a folder, no memory left for the index (it has no fixed limit, but always leaves 3 MB of ams.mitm's memory free), or an NSZ or XCZ compressed in solid mode, which cannot be read without decompressing it all: compress it again in block mode |
| HOME asks to update a game, or will not start it | The game was played with an update that is no longer on its drive: put the update back next to the game. If HOME offers to download it, choose Cancel |
| A `2505-xxxx` error | See the table below |

To report a problem, send `usbloadernx/logs/usbloadernx.log` from the SD card, without restarting first (each boot starts a new log), and for a crash the newest file in `atmosphere/crash_reports/` or `atmosphere/fatal_reports/`. `debug = u8!0x1` under `[usbloadernx]` adds more detail.

| Error | Meaning |
|---|---|
| `2505-0001` to `2505-0008` | The drive did not behave like USB storage (no bulk endpoints, failed or short commands, not ready, unsupported block size) |
| `2505-0009` | The file system could not be read: a loose connection, or a drive that needs to be checked on a computer |
| `2505-0010` to `2505-0014` | A dump that is not valid (XCI or HFS0 header, hash, partition, or keys this console does not have) |
| `2505-0015` | The drive was removed while it was being read |
| `2505-0016` | The content is not on any drive plugged in |
| `2505-0017` | No memory left for the index (it always leaves 3 MB of ams.mitm's memory free) |
| `2505-0018` | The game's drive is not plugged in |
| `2505-0019` | The drive stopped answering for 20 s and was reset |
| `2505-0020` | A file was written by a newer version of USB Loader NX and is left as it is |
| `2505-0021` to `2505-0023` | An NSZ or XCZ that cannot be read: a damaged header, solid mode (compress it again in block mode), or a damaged block |

## Building

Requires Docker.

```sh
git clone --recurse-submodules <repository>
scripts/build-image.sh     # once: builds the atms-build-libnx image
scripts/build.sh           # Atmosphère with USB Loader NX, and the Manager
scripts/package.sh         # dist/usb-loader-nx-<version>.zip: SD/ and USB/ folders and a README.md
```

The version comes from the closest `v*` tag (`git describe`), or `USBL_VERSION` when set.

How the repository is organized, and where changes go, is in [ARCHITECTURE.md](ARCHITECTURE.md).

## Updating Atmosphère

```sh
scripts/update-atmosphere.sh 1.13.0     # rebases the hook commits onto the new upstream tag
scripts/build.sh                        # build and test on the console
git -C atmosphere push --force-with-lease origin atmosphere
git add atmosphere ATMOSPHERE_VERSION
git commit -m "chore(atmosphere): update to 1.13.0"
```

A conflict stops the rebase inside `atmosphere/`, on the hook that no longer applies. Upstream tags are fetched into `refs/upstream/tags/` and never pushed, so they do not mix with our releases.

## Releases

Releases are made with `scripts/release.sh` on `master`, from the [Conventional Commits](https://www.conventionalcommits.org) history:

```sh
scripts/release.sh 0.1.0                # simulation: dist/release-v0.1.0/ with the zip, notes.md and CHANGELOG.md
scripts/release.sh 0.1.0 --publish      # builds on the PC, tags and publishes from here
scripts/release.sh 0.1.0 --publish-ci   # tags and pushes the tag; the Release workflow builds and publishes
```

Either way the Release workflow then commits the updated `CHANGELOG.md` to `master`; pull before the next change.

Versions `0.x` and versions with a `-` (`v0.2.0-beta.1`) are pre-releases. The notes and [CHANGELOG.md](CHANGELOG.md) come from [git-cliff](https://git-cliff.org) (`cliff.toml`) through `scripts/release-notes.sh`: commits grouped by type, each with its hash, pull request and author, the README's header on top, and the Atmosphère version and tested firmware in the notes. `git-cliff --unreleased` previews what the next release holds. Publishing needs `git-cliff` and `gh`; pushes and pull requests that change the code or the build are also built by the Build workflow.

## License

USB Loader NX © 2026 NedcloarBR. Based on Atmosphère © Atmosphère-NX.

USB Loader NX is a fork of [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere) and is licensed, like it, under the **GNU General Public License, version 2** (see [LICENSE](LICENSE)). If you share a build with anyone, share the matching source code with it.

### Third-party code

| Component | Used for | License | Where |
|---|---|---|---|
| [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere) | The custom firmware this fork extends | GPLv2 | `atmosphere/` |
| [FatFs](http://elm-chan.org/fsw/ff/) R0.16 | Reading exFAT and FAT32 drives | FatFs license (BSD-style) | `src/ams_mitm/usbl/fatfs/` |
| [stb_image](https://github.com/nothings/stb) and stb_image_write | Drawing the badge on game icons | MIT or public domain | `src/ams_mitm/usbl/third_party/stb/` |
| [Plutonium](https://github.com/XorTroll/Plutonium) | The Manager's user interface | MIT | `manager/lib/Plutonium/` |

Each component keeps its license file or header in the folder listed.

### Credits

[USB Loader GX](https://github.com/wiidev/usbloadergx) for the Wii is the inspiration for this project: like it, USB Loader NX lets you play your own game dumps straight from a USB drive, and the Manager's carousel, channel grid and Wii-menu-style dock follow its look. No code, artwork or sound from it is used.

### Legal

USB Loader NX is for game dumps you made from games you own. It ships no keys, firmware or game content.

Nintendo Switch is a trademark of Nintendo. This project is not affiliated with or endorsed by Nintendo, the Atmosphère team or the authors of USB Loader GX.
