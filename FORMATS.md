# File formats

USB Loader NX and its Manager keep every file in `usbloadernx/` on the SD card. Each file says which version of its format it was written in, so a newer or older USB Loader NX can tell what it is reading. The paths and version numbers are in [`common/include/usbl/paths.hpp`](common/include/usbl/paths.hpp), shared by the module and the Manager.

## Layout

```text
sdmc:/usbloadernx/
├── config.ini              module options (written by the module, changed from the Manager)
├── manager.ini             Manager options
├── templates/config.ini    every option of config.ini, documented (from the package; never read)
├── data/
│   ├── known_contents.bin  every content of every game indexed, so HOME does not see a game as missing while its drive is unplugged
│   ├── hidden_games.ini    games hidden from HOME in the Manager
│   └── favorites.ini       the Manager's favorites
├── cache/icons/<id>.jpg    the games' icons for the Manager while their drive is unplugged; can be deleted at any time
├── themes/<name>/          Manager themes (manager/themes/README.md)
├── i18n/<locale>.json      notification texts that replace the built-in ones
├── logs/usbloadernx.log    the module's log, started again at every boot
├── usb/                    virtual for homebrew: one folder per USB drive (its volume serial, A1B2-C3D4) with that drive's usbloadernx/usb/, read-only; empty on the card itself
└── games/                  reserved for playing from the SD card; not read yet
```

Not in `usbloadernx/`:

| File | Why |
|---|---|
| `atmosphere/config/system_settings.ini`, `[usbloadernx]` `enable` and `debug` | Atmosphère reads it at boot, before USB Loader NX starts |
| `usbloadernx/games/` on the USB drive | The dumps; read only |
| `usbloadernx/usb/` on the USB drive | Shared with homebrew, read-only, at `sdmc:/usbloadernx/usb/<volume serial>/` |

Versions before 1.0.0 kept the module's files in `atmosphere/config/usbloadernx/` (with `hidden_games.txt`), the log in `atmosphere/logs/`, and the Manager's in `config/usbloadernx-manager/` (with `favorites.txt` and `icons/`). On the first start the module moves its own files and the Manager moves its own; the two lists become `.ini` files. When the new place already has a file, that one stays and the old one is kept next to it as `<name>.old`. An old folder is removed only once it is empty.

## Versions

| File | Version | Where it is written |
|---|---|---|
| `config.ini`, `manager.ini`, `hidden_games.ini`, `favorites.ini` | 1 | `[format]` section, `version = 1` |
| `theme.json` | 1 | `"Format": { "Version": "1" }` |
| `known_contents.bin` | 1 | Header: `USLK`, version, count, reserved (4 × u32), then 0x38 bytes per content |
| `i18n/<locale>.json` | — | Keys only: an unknown key is ignored, a missing one keeps the built-in text |

A text file without a version is version 1, the format of the versions before 1.0.0.

| What is found | What happens |
|---|---|
| The same version | Read; `config.ini` and `manager.ini` keep the keys and sections they do not know when written again |
| An older version | Read, and written in the current version the next time it changes |
| A newer version | Read as far as it is understood, never written again unasked: the log says so and the Manager warns. `config.ini` is written again only after **Write anyway** in the Manager; the lists and `known_contents.bin` stay as they are until USB Loader NX is updated; Manager option changes last until it closes; a theme loads, marked in the picker as made for a newer version |

A version goes up only when a file can no longer be read the old way: a key moving or changing meaning. Adding a key does not change the version, since an older USB Loader NX ignores it (and `config.ini` and `manager.ini` keep it).

## `usbl` service

The Manager talks to the module through the `usbl` service ([`common/include/usbl/protocol.hpp`](common/include/usbl/protocol.hpp)). Its version is `VersionMajor.VersionMinor` (1.0 in USB Loader NX 1.0.0; 1.1 adds the `homebrew_access` option and the drives' volume serial), returned by `GetVersion` (command 20) with the module's version and a flag for each kind of file left untouched for a newer version.

| Change | Version |
|---|---|
| A new command, or a new field in bytes that were `reserved` | Minor + 1 |
| An existing command takes or returns something else | Major + 1 |

A Manager that finds another major version, or a module without `GetVersion` (before 1.0.0), shows which versions are installed and writes nothing. With a lower minor version it hides what the module does not have.

Commands 0 to 8 are for `ncm` and `loader`, built together with the module; they are not part of this version.
