# Architecture

How the repository is organized and where changes go.

## Two branches

The repository has two branches with no history in common.

### `master`: the project

```text
.
├── atmosphere/              → submodule of this repository, branch "atmosphere"
├── src/                     our code, compiled in place by the fork's makefiles
│   ├── ams_mitm/
│   │   ├── usbl/            the module: USB storage, FatFs, NCA, XCI/NSP, registration,
│   │   │                    config, notifications, badge
│   │   └── fs_mitm/         USB content in fsp-srv, and the hooks fs_mitm_service.cpp calls
│   ├── loader/              usbl client inside loader
│   └── ncm/                 usbl client inside ncm
├── common/include/usbl/     usbl service wire format, shared by the module and the Manager
├── usbl.mk                  adds src/ to the Atmosphère modules
├── manager/                 the Manager .nro
│   └── lib/Plutonium/       → submodule (UI library)
├── config_templates/        usbloadernx.ini, shipped in atmosphere/config_templates/
├── assets/                  project icon
├── tools/assets/            generators for the notification icons and the badge
├── docker/                  build image: Atmosphère's Dockerfile plus a newer libnx
├── scripts/                 build-image.sh · build.sh · package.sh · update-atmosphere.sh
└── ATMOSPHERE_VERSION       upstream release the fork is based on
```

### `atmosphere`: the fork

Atmosphère's history up to `ATMOSPHERE_VERSION`, plus one commit per hook:

```text
1.12.2 ─┬─ libstratosphere(ncm)  serve external content from the SD card storage
        ├─ ncm                   ask USB Loader NX about USB-served content
        ├─ libstratosphere(fs)   external code filesystem hook
        ├─ loader                mount the code of USB-served programs
        ├─ fs_mitm               route USB content, titlekeys and DLC to src/
        ├─ ams_mitm              run USB Loader NX as an ams.mitm module
        └─ settings              usbloadernx system settings and config template
```

The hooks only hold the entry points: a few lines in each upstream file that call into `src/`. Everything else is in `master`.

## How the pieces talk

- **`ams.mitm`** hosts the module (`src/ams_mitm/usbl/`): it owns the USB drives, indexes the dumps, registers the games with `ncm` and `ns`, and serves their content through its `fsp-srv` mitm.
- **`ncm`** and **`loader`** ask the module about USB-served content through the `usbl` service (`src/ncm/`, `src/loader/`).
- **The Manager** talks to the same `usbl` service. `common/include/usbl/protocol.hpp` is the single definition of its commands and structures.

## Changing code

- **Our code:** edit `src/`, `common/` or `manager/` and commit on `master`.
- **A hook in an Atmosphère file:** edit it in `atmosphere/` and commit there, on the `atmosphere` branch, one commit per hook. Push that branch, then run `git add atmosphere` on `master` to record the new fork commit.
- **A new `usbl` command:** add it to `common/include/usbl/protocol.hpp` first. Commands with more than one input take a single struct, because libstratosphere lays separate arguments out sorted by alignment.
