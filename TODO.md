# TODO

What is planned for USB Loader NX, roughly in the order it will be worked on. Done items move to the release notes.

## Manager

- [x] New design in the N-D-B palette: header with sort, search and drives; shelf (selected cover large, the next ones queued) and 5x2 grid; game window with details; drive window with usage bars; settings with sections and cards; focus ring with a glow
- [x] Project icon as the Manager's `icon.jpg` and header icon (from `assets/icon.svg`, USB trident redrawn)
- [x] Play time per game, from the console's play statistics
- [x] Favorites, search and sorting
- [x] Take a game shown only for a launch (hidden, or with `auto_register` off) off HOME as soon as it closes (module watches the application process)
- [x] Launch and Details buttons on the shelf; + settings, − exit, R view, X sort
- [x] Messages in the theme's colors (with the game's icon) instead of Plutonium's dialog
- [x] Warn about games whose update is gone while the console asks for a newer version, and about an update or DLC on another drive (shelf, grid, game window, launch)
- [ ] Hold a direction to repeat (lists, grid, the music volume slider)
- [ ] Study how CardsNX fetches custom per-game backgrounds from an online service through its API, and add something alike: the Manager's first network feature (opt-in), saving the downloaded art to the SD card or the USB drive. Comes before the network game loading
- [ ] Cover art from GameTDB for the carousel and grid, without the USB badge of the HOME icons (opt-in, after the background art above)
- [ ] Reset the launch required version of a game whose update is gone (as DBI's "Reset required version" does): first only copy the candidate system saves (0x8000000000000180, 0x8000000000000045) to the SD before and after DBI's reset to learn what changes; then a risky, emuMMC-only option with backup, double confirmation and reboot

## Themes

The Manager's look and sound as a theme anyone can make and share, like USB Loader GX themes on the Wii.

- [ ] Theme format: a folder in `sdmc:/config/usbloadernx-manager/themes/<name>/` with a `theme.json` (name, author, version, preview) and its assets; also as a `.zip`
- [x] Colors: every palette token (ground, panels, text, primary, warning, error, focus ring, alerts, overlay) overridable
- [ ] Layout: position and size of the toolbar, carousel, dock buttons, free space bar and windows; hide any of them
- [ ] Carousel and grid: number of covers, spacing, arc and scale of the carousel, columns and rows of the grid
- [x] Background image (static), dimmed by `Images.BackgroundDim`, with veils behind text over it
- [ ] Background image tiled
- [ ] Animated background: GIF decoded with giflib (devkitPro's SDL2_image for Switch is still 2.0.4, first frame only), capped at about 640x360 and 60 frames for the applet's memory; or a PNG strip with a frame count
- [ ] Frames, buttons and icons replaced by PNGs
- [x] Sounds: each effect replaced by a WAV, plus optional background music
- [x] Music volume slider (0 to 100) in the settings, and a larger audio buffer against crackling
- [x] Animations: speed and turning off the floating and background particles
- [x] Animations: focus ring pulse speed (`FocusPulse`)
- [ ] Animations: easing
- [ ] Fonts: a TTF/OTF in place of the console's shared font
- [x] Theme picker in the Manager's settings: a row of previews (`thumb.png`/`thumb.jpg`, else the background, else a sketch in the theme's colors), applied without restarting
- [x] A broken or partial theme falls back to the default for whatever it does not set

## PC Manager

A desktop app to manage a drive's dumps and installs from the computer, written in Rust.

- [ ] List the games on a drive (name, icon, version, update and DLC), read from the dumps themselves
- [ ] Copy dumps to `usbloadernx/games`, checking free space and file system first
- [ ] Find and fix fragmented files (copy them back contiguous)
- [ ] Remove a game with its update and DLC
- [ ] Install to the drive (the format the console reads with "Install to USB")
- [ ] Theme editor: build a Manager theme on the PC (colors, layout by dragging, images, sounds) with a live preview of the Manager's screens, and export it to the drive or SD card
- [ ] Windows, Linux and macOS builds

## Game formats

- [ ] Compressed dumps: NSZ, and XCZ in block mode (random access, needed to play without installing)
- [ ] Data patches for DLC (DLC with their own updates)
- [ ] Applications with more than one program (only program index 0 is served today)
- [ ] Higher limits for games with many DLC (32 per game, 512 NCAs and 64 files in total today)

## Storage

- [ ] Install to USB: copy NSP/XCI content to the drive (needs write support in FatFs)
- [ ] Move installed games, with their update and DLC, between the USB drive, the SD card and the console's storage (the emuMMC's when one is active), from the Manager, like the console's own "Move data"
- [ ] Play from the SD card without installing, with an SD badge on the icon
- [ ] Homebrew access to the drive (for example as `sdmc:/usb/`, read-only)

## Network

Play games from a PC or NAS on the local network, like Open PS2 Loader does over SMB on the PS2.

- [ ] Read dumps from an SMB share (`usbloadernx/games` on a PC or NAS), read-only, listed alongside the USB drives
- [ ] Network sources in `config.ini` (host, share, user), set up from the Manager
- [ ] A share behaves like one more drive: a game's update and DLC must be on the same share, and its games are hidden or shown like an unplugged drive's
- [ ] Read-ahead cache tuned for the dock's Ethernet port and for Wi-Fi, with a warning when the link is too slow for a game
- [ ] PC Manager as a ready-made server: share a folder with one click instead of setting up SMB

## System integration

- [x] A badge change shows on HOME as soon as it comes to the front: the games leave HOME and come back when the Manager loses focus or closes (HOME's grid keeps its own copy of each icon)
- [x] Romfs mods and translations (`atmosphere/contents/<id>/romfs`) on USB games, through Atmosphère's LayeredFS; holding the override button turns them off as for installed games
- [x] Serve the `ForBatchRead` data storage commands (firmware 23.0.0 and later), with romfs mods; `BatchRead` itself unsupported, as in Atmosphère
- [x] Tested on firmware 23.5.0: games, updates and romfs mods from USB
- [ ] More testers on 23.x, and removing the drive with a game running on 23.x
- [ ] Replace the `ncm` hooks with an `ncm` mitm inside `ams.mitm` (fewer upstream changes, no IPC hop)

## Performance

Measured before and after each change: the same game from USB and installed on the SD card (time to the title screen, loading a level).

- [x] Measure: read throughput (MB/s) and latency, indexing and registration time, in the debug log
- [x] Larger USB transfers: contiguous clusters read straight from the drive in 256 KB commands instead of one cluster (32 KB) at a time; 12 → 16 MB/s on a SanDisk Cruzer Blade, now the drive's own limit
- [ ] One lock per drive (and one for the index) instead of a single lock for everything; measured at 0 ms of waiting with one drive, so only worth it once two drives are read at the same time
- [ ] Read-ahead cache for sequential reads; not needed while the drive is the limit, to measure again with a faster drive
- [x] Faster indexing: a fragmented file's cluster map is built once per mount (2.7 s → 1.0 s)
- [x] Hide unplugged games sooner after boot: the grace period for drives plugged in at boot runs alongside the wait for the SD card database (HOME in line at ~52 s instead of ~58 s; the rest is ns finishing its own boot work)
- [ ] Manager: open faster with many games (icons and statuses loaded as needed)

## Known issues

- [x] An update on the drive is served only once the console has it registered (SD card ncm database), so with `auto_register` off a copied update does not change the game behind the console's back

## Testing

- [ ] USB-C port with an OTG adapter
- [ ] Switch Lite
- [ ] Hard drives that need a powered hub
- [ ] DLC larger than cosmetics (new levels or modes)

## Repository

- [ ] Workflow that opens a pull request when a new Atmosphère release rebases cleanly, and an issue when it does not
