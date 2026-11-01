# TODO

What is planned for USB Loader NX, roughly in the order it will be worked on. Done items move to the release notes.

## Manager

- [ ] New design: game carousel and channel grid, top toolbar, bottom bar with drive usage, game window, drive window
- [ ] Project icon as the Manager's `icon.jpg` (from `assets/icon.svg`)
- [ ] Play time per game, from the console's play statistics
- [ ] Favorites, search and sorting from the toolbar
- [ ] Re-hide a game launched while hidden as soon as it closes, not on the next Manager start
- [ ] Cover art from GameTDB (first feature that needs the network: opt-in)
- [ ] Themes

## Game formats

- [ ] Compressed dumps: NSZ, and XCZ in block mode (random access, needed to play without installing)
- [ ] Data patches for DLC (DLC with their own updates)
- [ ] Applications with more than one program (only program index 0 is served today)
- [ ] Higher limits for games with many DLC (32 per game, 512 NCAs and 64 files in total today)

## Storage

- [ ] Install to USB: copy NSP/XCI content to the drive (needs write support in FatFs)
- [ ] Play from the SD card without installing, with an SD badge on the icon
- [ ] Homebrew access to the drive (for example as `sdmc:/usb/`, read-only)

## System integration

- [ ] Serve the `ForBatchRead` data storage commands (firmware 23.0.0 and later)
- [ ] Replace the `ncm` hooks with an `ncm` mitm inside `ams.mitm` (fewer upstream changes, no IPC hop)
- [ ] Hide unplugged games sooner after boot (today it can take more than a minute)

## Known issues

- [ ] Deleting an update NSP from the drive leaves the update known, so it is served as "drive not connected"
- [ ] With `auto_register` off, an indexed update is served without being registered

## Testing

- [ ] USB-C port with an OTG adapter
- [ ] Switch Lite
- [ ] Hard drives that need a powered hub
- [ ] DLC larger than cosmetics (new levels or modes)

## Repository

- [ ] Workflow that opens a pull request when a new Atmosphère release rebases cleanly, and an issue when it does not
