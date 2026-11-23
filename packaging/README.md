# USB Loader NX {{VERSION}}

Play your Nintendo Switch games (`.xci` and `.nsp` files, or `.nsz` and `.xcz` compressed in block mode) straight from a USB drive or external hard drive, without taking space on the SD card.

## Before you start

- Your Switch must already run Atmosphère.
- The USB drive must be formatted as **exFAT** or **FAT32**.
- Back up the `atmosphere` folder of your SD card.

## Install

1. **SD card:** copy everything inside the `SD` folder of this package to the Switch's SD card. If your computer asks, choose **Replace**. This also updates your Atmosphère to {{ATMOSPHERE}} (the new `fusee.bin` goes to `bootloader/payloads`). Your other settings and mods stay in place.
2. **USB drive:** copy the `usbloadernx` folder from the `USB` folder of this package to the USB drive, and put your games in `usbloadernx/games`.
3. **Done:** put the SD card back, turn the Switch on as usual and plug the drive in (in the dock, or with a USB-C adapter). In a few seconds the games show up on the HOME menu, ready to play.

## Important

- A game's update and DLC must be on the **same drive** as the game.
- Do not unplug the drive while one of its games is running.
- If HOME says "The software needs to be updated", choose **Cancel**. It means the game was played with an update that is no longer on its drive: put the update back on the game's drive.
- Compressed games (`.nsz` and `.xcz`) do not work yet.

## The USB Loader NX Manager

Open it from the homebrew menu (the Album). It shows your games, how much space is left on the drives, and the settings.

| Button | Action |
|---|---|
| Left / Right | Pick a game |
| A | Play |
| Down | Launch and Details buttons |
| R | Switch between shelf and grid |
| Y | The USB drives |
| X | Change the order |
| ZL | Search by name |
| + | Settings: show or hide games on HOME, USB badge on the icons, sounds, themes |
| − | Exit |

The package includes the **Midnight** theme. To change themes: **+ > Manager > Theme**, pick one and press A.

## Something went wrong?

Take a photo of the screen and send it with `usbloadernx/logs/usbloadernx.log` from the SD card.

## Uninstall

1. In the Manager: **Settings > HOME Menu**, turn off "Show USB games on the HOME Menu".
2. Restore your backup of the `atmosphere` folder.
3. Restart the Switch.
