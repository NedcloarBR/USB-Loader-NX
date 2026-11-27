<div align="center">

<img src="assets/icon.svg" width="144" height="144" alt="USB Loader NX icon: a game card ending in a USB plug">

# USB Loader NX

Play your Nintendo Switch game dumps straight from a USB drive.<br>
An Atmosphère fork with a library manager in the spirit of USB Loader GX for the Wii.

[![Atmosphère 1.12.2](https://img.shields.io/badge/Atmosph%C3%A8re-1.12.2-2B2D31?labelColor=1E1F22)](https://github.com/Atmosphere-NX/Atmosphere)
![Tested on firmware 19.0.1 and 23.5.0](https://img.shields.io/badge/tested%20on-19.0.1%20%7C%2023.5.0-2B2D31?labelColor=1E1F22)
[![License GPLv2](https://img.shields.io/badge/license-GPLv2-00C26F?labelColor=1E1F22)](LICENSE)

[Features](README.md#features) · [Install](README.md#install) · [Configuration](README.md#configuration) · [Building](README.md#building) · [Architecture](ARCHITECTURE.md) · [README](README.md) · [TODO](TODO.md) · [License](README.md#license) · [Credits](README.md#credits)

</div>

Every release of USB Loader NX, newest first.

## v1.3.0 — 2026-11-25

### ✨ Features

- **usbl:** Report the game's folder under games/ ([`8fa328d`](https://github.com/NedcloarBR/USB-Loader-NX/commit/8fa328d484c8e1b7eca24136f393588e30e69c07) by @NedcloarBR)
- **manager:** Animated theme backgrounds ([`7f33daa`](https://github.com/NedcloarBR/USB-Loader-NX/commit/7f33daa10f5af40c111d813c0cb4aea775d02997) by @NedcloarBR)
- **manager:** Cover art and details from GameTDB ([`986e453`](https://github.com/NedcloarBR/USB-Loader-NX/commit/986e453fff1d17345dbfa20e3844f483dd43a943) by @NedcloarBR)
- **manager:** Theme components placed by the layout ([`3a0c833`](https://github.com/NedcloarBR/USB-Loader-NX/commit/3a0c83308ae0ef2f66bc25ef9dc19c748ef012e3) by @NedcloarBR)
- **manager:** Theme format 2, new views, filters and touch ([`706ca5b`](https://github.com/NedcloarBR/USB-Loader-NX/commit/706ca5b153cd1728718dff11e0fc09b6c4d7da6b) by @NedcloarBR)

### ♻️ Refactoring

- **usbl:** Keep only the embedded strings ([`acaa1a4`](https://github.com/NedcloarBR/USB-Loader-NX/commit/acaa1a4461f5cf2e72b17233664c73fb2f0f9250) by @NedcloarBR)

### 📝 Documentation

- Manager and themes for 1.3.0 ([`c033154`](https://github.com/NedcloarBR/USB-Loader-NX/commit/c03315499c6fe11dbdd86eaba6c9bede1dc49662) by @NedcloarBR)

### 🏗️ Build and CI

- **themes:** One theme spec for the schema and docs ([`49e79a8`](https://github.com/NedcloarBR/USB-Loader-NX/commit/49e79a8f7dd4858ea7e16c9a211101274ba5124d) by @NedcloarBR)

**Full changelog:** [v1.2.0...v1.3.0](https://github.com/NedcloarBR/USB-Loader-NX/compare/v1.2.0...v1.3.0)

## v1.2.0 — 2026-11-23

### ✨ Features

- **usbl:** Every ticket and title of a multi-title NSP ([`50ed3c4`](https://github.com/NedcloarBR/USB-Loader-NX/commit/50ed3c437f8da7c76bdc6d1b8721943ccddca35e) by @NedcloarBR)
- **usbl:** Play NSZ and XCZ dumps in block mode ([`0920578`](https://github.com/NedcloarBR/USB-Loader-NX/commit/0920578885b2c94385d5100c9299427c15e41a0f) by @NedcloarBR)
- **usbl:** Register DLC data patches ([`28421e4`](https://github.com/NedcloarBR/USB-Loader-NX/commit/28421e48d8330121e82d9b27d59a68d0fd4d6ebc) by @NedcloarBR)
- **usbl:** Serve applications with several programs ([`d19f3bd`](https://github.com/NedcloarBR/USB-Loader-NX/commit/d19f3bdc361ecb9dce4251deca7e2e1928761696) by @NedcloarBR)
- **usbl:** Keep what the indexing left out, and why ([`fed228d`](https://github.com/NedcloarBR/USB-Loader-NX/commit/fed228d020e71c543b239361957f59edf4a4dfa5) by @NedcloarBR)
- **manager:** Diagnostics section for files not read ([`7abe9c6`](https://github.com/NedcloarBR/USB-Loader-NX/commit/7abe9c68a8d8bb312dab4ae83058a1a0a6f73a1a) by @NedcloarBR)
- **fs_mitm:** Show the usb folder only while a drive is plugged in ([`c65514a`](https://github.com/NedcloarBR/USB-Loader-NX/commit/c65514a158a663f48c380345cde94c099bdee273) by @NedcloarBR)
- **usbl:** Mark compressed games on their badge ([`0e2cd52`](https://github.com/NedcloarBR/USB-Loader-NX/commit/0e2cd5232b1f822a3403d09e3e41fd8a81f16ec2) by @NedcloarBR)

### 🐛 Fixes

- **usbl:** Check installed copies per application ([`c81d384`](https://github.com/NedcloarBR/USB-Loader-NX/commit/c81d38473b4389ecd323f2b27b42310f6a3f37cf) by @NedcloarBR)
- **usbl:** Notification lines that fit ([`12e7d91`](https://github.com/NedcloarBR/USB-Loader-NX/commit/12e7d91a411901d3015cde65cca07f77c055c5a0) by @NedcloarBR)
- **usbl:** Read XCZ dumps, whose partitions have no hashes ([`7d2eea0`](https://github.com/NedcloarBR/USB-Loader-NX/commit/7d2eea00a0b5cebe03af841c5c6838db6ec1e318) by @NedcloarBR)
- **usbl:** Badge from the game itself, not its update ([`1858738`](https://github.com/NedcloarBR/USB-Loader-NX/commit/18587386f4a7d61c31dd2d47df98645b6ebda84c) by @NedcloarBR)

### ♻️ Refactoring

- **usbl:** Drop xci from names that cover NSPs too ([`ce73fef`](https://github.com/NedcloarBR/USB-Loader-NX/commit/ce73fef68ef9dd8c27ef471c98cccee9ec9284cb) by @NedcloarBR)

### 📝 Documentation

- Plan NSPs that hold several titles ([`86229ae`](https://github.com/NedcloarBR/USB-Loader-NX/commit/86229ae6c9ffaa6a06530ed5c4480b0c48e241e2) by @NedcloarBR)
- Game formats ([`4828161`](https://github.com/NedcloarBR/USB-Loader-NX/commit/48281614ae488e4bff947f29fa7f359e20bae5a1) by @NedcloarBR)

**Full changelog:** [v1.1.0...v1.2.0](https://github.com/NedcloarBR/USB-Loader-NX/compare/v1.1.0...v1.2.0)

## v1.1.0 — 2026-11-22

### ✨ Features

- **usbl:** Name each drive by its volume serial, add homebrew_access ([`3c37fba`](https://github.com/NedcloarBR/USB-Loader-NX/commit/3c37fba9cda6fe04b0dfb8591336998e094dc63c) by @NedcloarBR)
- **manager:** Homebrew access option and each drive's folder ([`bc39c43`](https://github.com/NedcloarBR/USB-Loader-NX/commit/bc39c43c1a8ee2c2f18cc850548869e39b94c7a6) by @NedcloarBR)
- **fs_mitm:** Let homebrew read the drives at sdmc:/usbloadernx/usb ([`c245a02`](https://github.com/NedcloarBR/USB-Loader-NX/commit/c245a02744b0c4f7163d83206fa6f554bb35538b) by @NedcloarBR)
- **usbl:** List a drive while it is being read ([`5121b00`](https://github.com/NedcloarBR/USB-Loader-NX/commit/5121b007694352212b4c2a17625045933ca2851c) by @NedcloarBR)
- **usbl:** Notifications as a title and a short list ([`8d59e8b`](https://github.com/NedcloarBR/USB-Loader-NX/commit/8d59e8bd8d66ad373c6ca5c8091a3311da5ee250) by @NedcloarBR)
- **manager:** Show each drive's details as tags ([`cbc4b60`](https://github.com/NedcloarBR/USB-Loader-NX/commit/cbc4b6070c4f01eaad51debd5a9b154c1307aae4) by @NedcloarBR)
- Choose the language of the Manager and the notifications ([`abba816`](https://github.com/NedcloarBR/USB-Loader-NX/commit/abba8168e839c9dd373f67a6a3edecfe037d9981) by @NedcloarBR)

### 🐛 Fixes

- **usbl:** Size a drive by its mounted volume ([`9102f35`](https://github.com/NedcloarBR/USB-Loader-NX/commit/9102f35eadfeee002a3e4d9edc0e4e677ea6ef35) by @NedcloarBR)
- **usbl:** Wait for hard drives to spin up ([`9a9601f`](https://github.com/NedcloarBR/USB-Loader-NX/commit/9a9601f34b745b7a7dc1cbe506c480c1a4f0b90c) by @NedcloarBR)

### ⚡ Performance

- **usbl:** Take the FAT32 free space from FSInfo ([`38db127`](https://github.com/NedcloarBR/USB-Loader-NX/commit/38db1275e1c77667886efcc06a4093d073619837) by @NedcloarBR)

### 📝 Documentation

- Homebrew access to the USB drives ([`b7fc08c`](https://github.com/NedcloarBR/USB-Loader-NX/commit/b7fc08c771ce0f24fc058731f311e61dfe5daa82) by @NedcloarBR)

**Full changelog:** [v1.0.0...v1.1.0](https://github.com/NedcloarBR/USB-Loader-NX/compare/v1.0.0...v1.1.0)

## v1.0.0 — 2026-11-21

### ✨ Features

- Keep every file in sdmc:/usbloadernx with versioned formats ([`84edefd`](https://github.com/NedcloarBR/USB-Loader-NX/commit/84edefda9b8e3fc9ee668b57aacaff32d097c55a) by @NedcloarBR)
- **manager:** Check the system on start and turn USB Loader NX on ([`994f796`](https://github.com/NedcloarBR/USB-Loader-NX/commit/994f796cf524ce038c06707e13dae15d767ea5bc) by @NedcloarBR)
- **manager:** Repeat a held direction ([`2719416`](https://github.com/NedcloarBR/USB-Loader-NX/commit/2719416042dbe739f2458db1d69e635a4d10316e) by @NedcloarBR)
- **manager:** Install themes from a .zip ([`fa1ba09`](https://github.com/NedcloarBR/USB-Loader-NX/commit/fa1ba0919b8f65c2dccd7a7985093dd5ba645ab6) by @NedcloarBR)
- **usbl:** Say which files on the drive were left out ([`957a2c5`](https://github.com/NedcloarBR/USB-Loader-NX/commit/957a2c50062928e599830da3511dfe5b0e1bb7cf) by @NedcloarBR)
- **usbl:** Grow the index with the drives instead of fixed limits ([`77a973e`](https://github.com/NedcloarBR/USB-Loader-NX/commit/77a973e6cfb6e263b40cebd64b50bde86a5c6eb7) by @NedcloarBR)

### 🐛 Fixes

- **usb:** Give up on a drive that stops answering after 20 s ([`480b9d4`](https://github.com/NedcloarBR/USB-Loader-NX/commit/480b9d46773651baa555ea504d9d48e2b6a9d531) by @NedcloarBR)
- **usbl:** Keep hidden games in the Manager after boot ([`1f52d8c`](https://github.com/NedcloarBR/USB-Loader-NX/commit/1f52d8c5b3dabed8b30a87aa1523a002ba4e05fa) by @NedcloarBR)

### 📝 Documentation

- Drop done TODO items and update the tested hardware ([`9281208`](https://github.com/NedcloarBR/USB-Loader-NX/commit/92812081cca3de0dc03726434eef9e575da4fc32) by @NedcloarBR)
- Common problems, linked badges and the new limits ([`9966aed`](https://github.com/NedcloarBR/USB-Loader-NX/commit/9966aed0cc11afc1407339b7711098aa5c6bcdde) by @NedcloarBR)

### 🏗️ Build and CI

- Move the actions to Node 24 ([`8c4165a`](https://github.com/NedcloarBR/USB-Loader-NX/commit/8c4165a5322d55eccd9d9a827e9f5c0b1a621b24) by @NedcloarBR)
- Cache the build images between runs ([`a54034d`](https://github.com/NedcloarBR/USB-Loader-NX/commit/a54034d0a2287f0318a20348d3a52750e255c720) by @NedcloarBR)

**Full changelog:** [v0.4.0...v1.0.0](https://github.com/NedcloarBR/USB-Loader-NX/compare/v0.4.0...v1.0.0)

## v0.4.0 — 2026-11-21

### ✨ Features

- **fs_mitm:** Serve the ForBatchRead data storages of firmware 23 ([`94c93e8`](https://github.com/NedcloarBR/USB-Loader-NX/commit/94c93e82ea425a116a080cf00ca59f505a52f709) by @NedcloarBR)

### 📝 Documentation

- Point to the log in atmosphere/logs ([`4b51d83`](https://github.com/NedcloarBR/USB-Loader-NX/commit/4b51d838548937d17010cb9a6b846057e2b7023d) by @NedcloarBR)
- Mark firmware 23.5.0 as tested ([`2f3938a`](https://github.com/NedcloarBR/USB-Loader-NX/commit/2f3938ab5e3ccfb62294052c409f6e5b61613241) by @NedcloarBR)

**Full changelog:** [v0.3.0...v0.4.0](https://github.com/NedcloarBR/USB-Loader-NX/compare/v0.3.0...v0.4.0)

## v0.3.0 — 2026-11-20

### ✨ Features

- **usbl:** Log read statistics with usbloadernx!debug ([`76441d2`](https://github.com/NedcloarBR/USB-Loader-NX/commit/76441d2ed83d2419d01f40953b3fed13c6e6e0bd) by @NedcloarBR)

### ⚡ Performance

- **usbl:** Read contiguous clusters in 256 KB transfers ([`3d53c52`](https://github.com/NedcloarBR/USB-Loader-NX/commit/3d53c52fc1b9937337333fffe8820c825e8ecf63) by @NedcloarBR)
- **usbl:** Build a fragmented file's cluster map once per mount ([`f7268fd`](https://github.com/NedcloarBR/USB-Loader-NX/commit/f7268fdbdcb5947766f6fd0d4864dc08391f159d) by @NedcloarBR)
- **usbl:** Count the boot grace period from the USB host start ([`7fe9d30`](https://github.com/NedcloarBR/USB-Loader-NX/commit/7fe9d309e1bd2da917f876b257174d06b5271361) by @NedcloarBR)

### 📝 Documentation

- **todo:** Plan the performance work for 1.0.0 ([`473f1e8`](https://github.com/NedcloarBR/USB-Loader-NX/commit/473f1e88fe8985dd7de547f5854c92adf470ad94) by @NedcloarBR)
- **readme:** Say when the Build workflow runs ([`0f78c53`](https://github.com/NedcloarBR/USB-Loader-NX/commit/0f78c5353b861a3d81572b98234a16f95f4f9b93) by @NedcloarBR)
- **todo:** Mark the measured performance work ([`d85a706`](https://github.com/NedcloarBR/USB-Loader-NX/commit/d85a7068335574a95e426e0491fb465aa71ce6ec) by @NedcloarBR)

**Full changelog:** [v0.2.0...v0.3.0](https://github.com/NedcloarBR/USB-Loader-NX/compare/v0.2.0...v0.3.0)

## v0.2.0 — 2026-11-20

### ✨ Features

- **usbl:** Apply romfs mods and translations to USB games ([`3375cac`](https://github.com/NedcloarBR/USB-Loader-NX/commit/3375cac723ebb802de3e7a2856535ce994d08c52) by @NedcloarBR)

### 🐛 Fixes

- **usbl:** Serve an update only once the console has it registered ([`ef03f71`](https://github.com/NedcloarBR/USB-Loader-NX/commit/ef03f7179e864d01feeb2d91390c48a8477f77b0) by @NedcloarBR)
- **usbl:** Keep a running game's record when its drive is removed ([`fcdb78b`](https://github.com/NedcloarBR/USB-Loader-NX/commit/fcdb78ba164215bde80535a459df138b4e5223b4) by @NedcloarBR)

### 🏗️ Build and CI

- **build:** Run only when the build inputs change ([`20b9aeb`](https://github.com/NedcloarBR/USB-Loader-NX/commit/20b9aebc8eb97bf684bb95d742875593fcf1006c) by @NedcloarBR)

### 🔧 Other

- **github:** Add issue and pull request templates ([`b22da33`](https://github.com/NedcloarBR/USB-Loader-NX/commit/b22da33f2c3e8582df54b266e703ab5297161851) by @NedcloarBR)

**Full changelog:** [v0.1.0...v0.2.0](https://github.com/NedcloarBR/USB-Loader-NX/compare/v0.1.0...v0.2.0)

## v0.1.0 — 2026-11-20

### ✨ Features

- **usbl:** Add USB mass storage driver and volume manager ([`cdaf3d3`](https://github.com/NedcloarBR/USB-Loader-NX/commit/cdaf3d3fe100903b9ae9156fe01b2936f109e3f3) by @NedcloarBR)
- **usbl:** Decrypt NCAs with keys derived on demand ([`21ac1ae`](https://github.com/NedcloarBR/USB-Loader-NX/commit/21ac1aec3359f4f64f27f7f67c37b6ed984c68dc) by @NedcloarBR)
- **usbl:** Index XCI and NSP dumps found on USB ([`b1aeea0`](https://github.com/NedcloarBR/USB-Loader-NX/commit/b1aeea0e3ab7734d3009b0388a094521bee810b5) by @NedcloarBR)
- **ncm:** Query USB Loader NX for USB-served content ([`44206b5`](https://github.com/NedcloarBR/USB-Loader-NX/commit/44206b5a8c5c040d3080bb793a2d00db6bd954c2) by @NedcloarBR)
- **loader:** Mount code of USB-served programs ([`f9c3f85`](https://github.com/NedcloarBR/USB-Loader-NX/commit/f9c3f85a0f4358c4b89635892e2cd19cbc8f963b) by @NedcloarBR)
- **usbl:** Register USB titles with updates and DLC in ncm and ns ([`e8c2642`](https://github.com/NedcloarBR/USB-Loader-NX/commit/e8c264237cb9362b47e300b41d1380d206fbfcbf) by @NedcloarBR)
- **fs_mitm:** Serve USB content, titlekeys and DLC storage ([`cc927bc`](https://github.com/NedcloarBR/USB-Loader-NX/commit/cc927bc612f9f2bbaaf09cccbe7baae6e66aa558) by @NedcloarBR)
- **usbl:** Post Ultrahand notifications in pt-BR and en-US ([`ae9cc81`](https://github.com/NedcloarBR/USB-Loader-NX/commit/ae9cc81a9b0668661086569e62a9c58a134dd450) by @NedcloarBR)
- **usbl:** Badge icons of USB-served games ([`d577c4e`](https://github.com/NedcloarBR/USB-Loader-NX/commit/d577c4e5e1dc08742c3fbba476b83ffa7638d28b) by @NedcloarBR)
- **usbl:** Run as an ams.mitm module ([`cb7cde4`](https://github.com/NedcloarBR/USB-Loader-NX/commit/cb7cde4196cf40796335d170b5f827cd215f84e7) by @NedcloarBR)
- **usbl:** Tell sleep from removal through psc ([`958ba70`](https://github.com/NedcloarBR/USB-Loader-NX/commit/958ba70479c55acf16e570b0c7b552f591baf438) by @NedcloarBR)
- **usbl:** Add config.ini and usbloadernx system settings ([`4d9650b`](https://github.com/NedcloarBR/USB-Loader-NX/commit/4d9650bd4620fd4fc6cd3a9ca362fedbb3596ed1) by @NedcloarBR)
- **usbl:** Hide games from HOME per title ([`d07651e`](https://github.com/NedcloarBR/USB-Loader-NX/commit/d07651ecbca2664f91624fce360b01295146bd40) by @NedcloarBR)
- **usbl:** Report each game's size on the drive ([`7efdee6`](https://github.com/NedcloarBR/USB-Loader-NX/commit/7efdee6cd67a1c636c4090b6049ac03f3dde67ba) by @NedcloarBR)
- **usbl:** Apply HOME visibility options right away ([`b6f7160`](https://github.com/NedcloarBR/USB-Loader-NX/commit/b6f71601be2d9aab6ed9f0fecf9a2a24f4ad0348) by @NedcloarBR)
- **usbl:** Show a game on HOME just to launch it ([`88dc705`](https://github.com/NedcloarBR/USB-Loader-NX/commit/88dc7050035ead239ba113f4c42e2a3dc438bccd) by @NedcloarBR)
- **usbl:** Give the Manager plain icons and display versions ([`17ed142`](https://github.com/NedcloarBR/USB-Loader-NX/commit/17ed1428209ce3afc2fd6c89f72b3ea2f307ae59) by @NedcloarBR)
- **usbl:** Keep a game's update and DLC on its own drive ([`f7bbaf0`](https://github.com/NedcloarBR/USB-Loader-NX/commit/f7bbaf0caed190d6337c640f5813a790d635a458) by @NedcloarBR)
- **usbl:** Take a launched game off HOME once it closes ([`8388add`](https://github.com/NedcloarBR/USB-Loader-NX/commit/8388adde82949d1172c5d71402dc3e14bd9ba5cc) by @NedcloarBR)
- **usbl:** Show a badge change on HOME right away ([`4d0d5fe`](https://github.com/NedcloarBR/USB-Loader-NX/commit/4d0d5fe81c10268f39abf19ef09afa804cc2e226) by @NedcloarBR)
- **manager:** Add the USB Loader NX Manager ([`57a5124`](https://github.com/NedcloarBR/USB-Loader-NX/commit/57a512493637ddd03e47a3e7ad11e8d8a77e333f) by @NedcloarBR)

### 🐛 Fixes

- **manager:** Link the repository in About ([`4ceee33`](https://github.com/NedcloarBR/USB-Loader-NX/commit/4ceee338201cde7098bd8f6d3ba489813a72ef7c) by @NedcloarBR)

### 🧩 Atmosphère

- Update to 1.12.2 ([`af54443`](https://github.com/NedcloarBR/USB-Loader-NX/commit/af54443aa74fc1186c8d2dddc8543ab5a0c0968f) by @NedcloarBR)

### ♻️ Refactoring

- **usbl:** Share the usbl protocol between module and Manager ([`a3c1f91`](https://github.com/NedcloarBR/USB-Loader-NX/commit/a3c1f9116d0bf6bae50350ecc4a07be2d0da8a78) by @NedcloarBR)

### 📝 Documentation

- Add README, license, architecture notes and TODO ([`ae25820`](https://github.com/NedcloarBR/USB-Loader-NX/commit/ae25820c6fb5b961c4f20b903daa81665c8de9d6) by @NedcloarBR)
- Plan the Manager, themes, PC Manager and network loading ([`79afe0e`](https://github.com/NedcloarBR/USB-Loader-NX/commit/79afe0ed1daceb20dcf4dfa286073cd01b712ad4) by @NedcloarBR)
- **manager:** Add the Manager README ([`ca344e7`](https://github.com/NedcloarBR/USB-Loader-NX/commit/ca344e75628210c03b7c4cea0fc8678c02f026aa) by @NedcloarBR)

### 🏗️ Build and CI

- Compile src/ into the Atmosphère fork branch ([`b97d632`](https://github.com/NedcloarBR/USB-Loader-NX/commit/b97d632067ae5a8cd6b855ba4953f21ca2a528f3) by @NedcloarBR)
- Add build image, scripts and release workflows ([`6515f2f`](https://github.com/NedcloarBR/USB-Loader-NX/commit/6515f2f08c55f59761be8e7eda3c474b7381fd4c) by @NedcloarBR)
- **release:** Generate notes and CHANGELOG.md with git-cliff ([`9314334`](https://github.com/NedcloarBR/USB-Loader-NX/commit/9314334a3bd52396f0633a0ea8f1ae37cb7c1210) by @NedcloarBR)
- **release:** Let the Release workflow keep CHANGELOG.md ([`01ae0f4`](https://github.com/NedcloarBR/USB-Loader-NX/commit/01ae0f489d5eaee089efd1ef955e07c56c7c1bcd) by @NedcloarBR)
- **build:** Build the images with plain docker ([`ef3fe5b`](https://github.com/NedcloarBR/USB-Loader-NX/commit/ef3fe5b3b2a3b7f7b63b6ee35c87c329dc1eb34a) by @NedcloarBR)

### 🔧 Other

- **usbl:** Vendor FatFs R0.16 as read-only ([`d9abc2f`](https://github.com/NedcloarBR/USB-Loader-NX/commit/d9abc2fa3dc4b344ff70d6a3b75f069f82b8b875) by @NedcloarBR)
- **package:** Ship the release as SD and USB folders with a README ([`908d76f`](https://github.com/NedcloarBR/USB-Loader-NX/commit/908d76f9d43a9aa6fd97a19d7436be36ba5f3e26) by @NedcloarBR)

**Full changelog:** [v0.1.0](https://github.com/NedcloarBR/USB-Loader-NX/commits/v0.1.0)


