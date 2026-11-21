<div align="center">

<img src="icon.jpg" width="144" height="144" alt="USB Loader NX Manager icon: a game card ending in a USB plug">

# USB Loader NX Manager

The homebrew app of [USB Loader NX](../README.md): your USB games as a shelf or a grid, with play time, favorites, search, drive usage and the settings.

[Using it](#using-it) · [Settings](#settings) · [Files on the SD card](#files-on-the-sd-card) · [Themes](themes/README.md) · [Translations](#translations) · [Building](#building) · [Code](#code)

</div>

`USBLoaderNX-Manager.nro` talks to the USB Loader NX module through its `usbl` service, so it needs the Atmosphère of this project running. Without it, it says the service is not active and how to turn it on.

## Using it

Open it from the homebrew menu (the Album).

| Screen | What it shows |
|---|---|
| **Shelf** | The selected game large, the next ones queued after it; under it the drive, version, DLC, size, play time and anything that needs attention |
| **Grid** | Five covers a row, names on up to two lines, a badge on games whose drive is unplugged or whose update is missing or on another drive |
| **Game details** | Cover, format and drive; version, update, DLC, size, file fragmentation, play time and last played; Launch and Hide from / Show on HOME |
| **USB drives** | Each drive's name, file system, games, updates and DLC, free space, and a usage bar that turns from green to red as it fills |
| **Settings** | HOME Menu, Manager and About sections |

| Button | Action |
|---|---|
| Left / Right | Pick a game (Up / Down too on the grid) |
| A | Shelf: launch the game. Grid: open its details |
| Down | Shelf: move to the Launch and Details buttons (Up or B goes back) |
| R | Switch between shelf and grid |
| Y | USB drives; in a game's details, add to or remove from favorites |
| X | Change the order: name, recently played, most played, size |
| ZL | Search by name with the console's keyboard (B clears the search) |
| L / R in a game's details | Previous or next game |
| + | Settings |
| − | Exit |

Favorites come first in every order. A game hidden from HOME, or every game while "Show USB games on the HOME Menu" is off, can still be launched here: it shows on HOME while it runs and leaves again once it is closed.

When a game was already played with an update that is no longer on its drive, the console asks for that version: the Manager says so before launching instead of letting HOME offer a download.

## Settings

| Section | Option | Kept in |
|---|---|---|
| HOME Menu | Show USB games on the HOME Menu | the module's `config.ini` |
| HOME Menu | Hide games while their drive is unplugged | the module's `config.ini` |
| HOME Menu | USB badge on icons (HOME shows the change as soon as it comes to the front) | the module's `config.ini` |
| HOME Menu | Notifications (Ultrahand) | the module's `config.ini` |
| Manager | Sound effects | `manager.ini` |
| Manager | Theme, picked from a row of previews | `manager.ini` |
| Manager | Theme music, and its volume (0 to 100 %) | `manager.ini` |

## Files on the SD card

Everything the Manager writes is under `sdmc:/usbloadernx/`, next to the module's files (see [FORMATS.md](../FORMATS.md)):

| File | What it holds |
|---|---|
| `manager.ini` | `sounds`, `sort`, `theme`, `music` and `music_volume` under `[manager]` |
| `data/favorites.ini` | `<application id> = 1` under `[favorites]` |
| `cache/icons/` | The games' own icons (without the HOME badge), kept for when their drive is unplugged |
| `themes/<name>/` | Installed themes, see [themes/README.md](themes/README.md) |

It also reads the console's play records (`pdm`) for play time and the launch version (`avm`) to warn about a missing update; neither is written.

## Translations

The text is in `romfs/i18n/<locale>.json` (`en-US` and `pt-BR` today), one section per screen and `{{NAME}}` placeholders. English is loaded first, then the console's language, so a missing key falls back to English. A new language is one more file with the same keys, built into the app.

## Building

From the repository root, with the build image from the main README:

```sh
scripts/build.sh manager     # manager/USBLoaderNX-Manager.nro
```

The icon (`icon.jpg`, also in `romfs/`), the sound effects (`romfs/sfx/`) and the Midnight theme's background, preview and music are made by the scripts in `tools/assets/`.

## Code

Written with [Plutonium](https://github.com/XorTroll/Plutonium) (`lib/Plutonium`, submodule). The design is laid out at 1280x720 and scaled to the screen; every shape is drawn antialiased.

| File | What it does |
|---|---|
| `source/main.cpp` | Renderer options, and telling the module when HOME comes to the front |
| `source/main_application.cpp` | Screens, polling the module for drive changes, launching, sort and search |
| `source/game_library.cpp` | The games: status from the module, names and icons, play time, favorites, the launch version check |
| `source/usbl_client.cpp` | The `usbl` service commands (`common/include/usbl/protocol.hpp`) |
| `source/ui/library_screen.cpp` | Shelf, grid, game details, USB drives and messages |
| `source/ui/settings_screen.cpp` | Settings, the theme picker and the music volume slider |
| `source/ui/theme.cpp` | Palette and theme loading (`theme.json`) |
| `source/ui/draw.cpp` | Antialiased shapes, the focus ring, the usage bar and the icons |
| `source/ui/widgets.cpp` | Text that fits, wrapping, button hints |
| `source/sound.cpp` | Sound effects and theme music (SDL_mixer) |
| `source/strings.cpp` | The translations |
| `source/manager_settings.cpp` | `manager.ini` |

## License

GPLv2, like the rest of USB Loader NX. Plutonium is MIT.
