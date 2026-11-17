<div align="center">

<img src="https://i.imgur.com/hjRlUTe.png" width="180" alt="KaamoClubModApi Logo" />

# KaamoClubModApi

**A modding api for the game Galaxy on Fire 2**

[![Maintained](https://img.shields.io/badge/Maintained-yes-brightgreen?style=flat-square)](https://github.com/1337Skid/KaamoClubModApi)
[![License](https://img.shields.io/github/license/1337Skid/KaamoClubModApi?style=flat-square)](LICENSE)
[![Language](https://img.shields.io/badge/Language-C%2B%2B%20%7C%20Rust-blue?style=flat-square)](https://github.com/1337Skid/KaamoClubModApi)
[![Build](https://img.shields.io/github/actions/workflow/status/1337Skid/KaamoClubModApi/build.yml?style=flat-square&label=CI)](https://github.com/1337Skid/KaamoClubModApi/actions)
[![Stars](https://img.shields.io/github/stars/1337Skid/KaamoClubModApi?style=flat-square)](https://github.com/1337Skid/KaamoClubModApi/stargazers)
[![Forks](https://img.shields.io/github/forks/1337Skid/KaamoClubModApi?style=flat-square)](https://github.com/1337Skid/KaamoClubModApi/network/members)

</div>

---

## Overview

KaamoClubModApi is a modding api for **Galaxy on Fire 2** on PC. It injects via a proxy dll and exposes a clean lua scripting language. Whether you want to tweak gameplay or add new behaviors, this is your foundation.

---

## Project Structure

| Folder | Description |
|---|---|
| `modapi/` | core modding api, the main C++ library your mods link against |
| `mods_examples/` |  Working example mods to learn from |
| `proxydll/` | Rust based proxy dll that launch the api on game launch |
| `.github/workflows/` | ci/cd pipelines for automated builds |

---

## Installation

1. Download the latest **release zip** from [Releases](https://github.com/1337Skid/KaamoClubModApi/releases), or grab the latest **build artifact** straight from [GitHub Actions](https://github.com/1337Skid/KaamoClubModApi/actions) (Warning: you are still missing GOF2.exe if you are downloading from the github actions).
2. Extract and drop everything (GOF2.exe+d3d9.dll+kaamoclubmodapi.dll) into your Galaxy on Fire 2 folder.
3. Launch the game and the proxy dll will load automatically.

---

## Making Mods

A full documentation is at https://gof2modding.memoryleak.space. The documentation cover the basics of events,hooking and are kept up to date with the latest api changes.

---

## Building from Source

**Requirements:** Windows, [xmake](https://xmake.io)

```bash
xmake
```

That's it. xmake handles the rest so both the C++ modapi and the Rust proxy dll.

---

## Troubleshooting

Every offset of the api is an absolute address inside one specific `GOF2.exe`. At startup the api prints a fingerprint of your exe in the console:

```
[+] GOF2.exe fingerprint: base=0x00400000 timestamp=0x... sizeofimage=0x... checksum=0x... entry=0x... codecrc32=0x... (build not in the known builds table)
```

- **Safe mode**: if the exe is loaded at another address (ASLR), if the `Globals::init` hook can't be installed, or if more than 25% of the hooks fail, the api disables itself for that session, shows a message box and the game runs without mods. This is almost always a different `GOF2.exe` build than the one the api was made for.
- **A hook that fails** (but not enough to trigger safe mode) is printed as `[-] hook NAME (0xADDRESS) was not installed: reason`; the mods that depend on it won't work.
- **Crash logs**: a crash writes `crashlogs/crash_<time>.txt` and a matching `.dmp` minidump. The text log lists the exe fingerprint, the loaded mods, the hooks that failed to install, and names game addresses (`GOF2.exe+0xRVA | near HOOK_NAME+0xOFFSET`). Please attach it to your bug reports.
- Mods are loaded in alphabetical order of their folder name.

If you tested the api with another `GOF2.exe`, add its fingerprint to `kKnownBuilds` in `modapi/src/safety.cpp`.

---

## Contributors

Thanks to everyone who helped make this happen:

- **1337Skid** — Main dev
- **Elcapor** — contributor (a proof of concept of a custom system and station)
- **RuslanchikX** — contributor (The idea to use structs instead of raw reading a pointer)
- **[Baal Netbek](https://github.com/BaalNetbek)** — contributor (a proof of concept of custom meshes and custom textures)

PRs and issues are welcome. If you make a mod using this api feel free to share it!

---

<div align="center">

Released under the [GPL-3.0 License](LICENSE)

</div>
