# TODO

What is planned for USB Loader NX, roughly in the order it will be worked on. Done items move to the release notes.

## Manager

- [ ] Study how CardsNX fetches custom per-game backgrounds from an online service through its API, and add something alike: the Manager's first network feature (opt-in), saving the downloaded art to the SD card or the USB drive. Comes before the network game loading
- [ ] Cover art from GameTDB for the carousel and grid, without the USB badge of the HOME icons (opt-in, after the background art above)
- [ ] Download missing covers and art from GameTDB in the Manager itself, for every game on the drives
- [ ] Shelves or folders view: the subfolders of `usbloadernx/games/` as categories (needs the GameTDB art above)
- [ ] Reset the launch required version of a game whose update is gone (as DBI's "Reset required version" does): first only copy the candidate system saves (0x8000000000000180, 0x8000000000000045) to the SD before and after DBI's reset to learn what changes; then a risky, emuMMC-only option with backup, double confirmation and reboot

## Themes

The Manager's look and sound as a theme anyone can make and share, like USB Loader GX themes on the Wii.

- [ ] Layout: position and size of the toolbar, carousel, dock buttons, free space bar and windows; hide any of them
- [ ] Carousel and grid: number of covers, spacing, arc and scale of the carousel, columns and rows of the grid
- [ ] Background image tiled
- [ ] Animated background: GIF decoded with giflib (devkitPro's SDL2_image for Switch is still 2.0.4, first frame only), capped at about 640x360 and 60 frames for the applet's memory; or a PNG strip with a frame count
- [ ] Frames, buttons and icons replaced by PNGs
- [ ] Animations: easing
- [ ] Fonts: a TTF/OTF in place of the console's shared font

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

## Storage

- [ ] Install to USB: copy NSP/XCI content to the drive (needs write support in FatFs)
- [ ] Homebrew writing to the drive's `usbloadernx/usb/`: creating folders first (some homebrew create their own folders inside the one they are pointed at), then files; needs write support in FatFs and a safe way to unplug
- [ ] Move installed games, with their update and DLC, between the USB drive, the SD card and the console's storage (the emuMMC's when one is active), from the Manager, like the console's own "Move data"
- [ ] Play from the SD card without installing, with an SD badge on the icon

## Network

Play games from a PC or NAS on the local network, like Open PS2 Loader does over SMB on the PS2.

- [ ] Read dumps from an SMB share (`usbloadernx/games` on a PC or NAS), read-only, listed alongside the USB drives
- [ ] Network sources in `config.ini` (host, share, user), set up from the Manager
- [ ] A share behaves like one more drive: a game's update and DLC must be on the same share, and its games are hidden or shown like an unplugged drive's
- [ ] Read-ahead cache tuned for the dock's Ethernet port and for Wi-Fi, with a warning when the link is too slow for a game
- [ ] PC Manager as a ready-made server: share a folder with one click instead of setting up SMB

## System integration

- [ ] Replace the `ncm` hooks with an `ncm` mitm inside `ams.mitm` (fewer upstream changes, no IPC hop)

## Performance

Measured before and after each change: the same game from USB and installed on the SD card (time to the title screen, loading a level).

- [ ] One lock per drive (and one for the index) instead of a single lock for everything; measured at 0 ms of waiting with one drive, so only worth it once two drives are read at the same time
- [ ] Read-ahead cache for sequential reads; not needed while the drive is the limit, to measure again with a faster drive
- [ ] Manager: open faster with many games (icons and statuses loaded as needed)

## Testing

- [ ] Hard drives with their own supply or on a powered hub (a 2.5" drive powered by the console works)
- [ ] Open a drive plugged in while another is still being registered on HOME, instead of waiting for it
- [ ] DLC larger than cosmetics (new levels or modes)

## Repository

- [ ] Compiler cache (ccache) for the Atmosphère build, the 18 of the 23 minutes
- [ ] Workflow that opens a pull request when a new Atmosphère release rebases cleanly, and an issue when it does not
