# Features

> Part of the [Ymir documentation](README.md). A user-level tour of what Ymir does, with pointers to where each
> feature lives in the code. For installation and usage see the main [README](../../README.md); for problems see
> [TROUBLESHOOTING.md](../../TROUBLESHOOTING.md).

## Running games

| Feature | Details | Code |
|---|---|---|
| Disc images | MAME CHD, BIN/CUE (with WAVE, MP3 or OGG audio tracks), IMG/CCD/SUB, MDF/MDS, ISO | `libs/ymir-core/.../media/loader/` |
| Physical drives | Read real discs from the host's optical drive (feature flag, local builds) | `media/cd_device/cd_device_host.*`, `host_cd_*.cpp` |
| BIOS (IPL) | Automatic detection and region-aware selection of BIOS images in `roms/ipl/`, or a fixed override | `app/rom_manager.*`, `services/rom_service.*`, `db/ipl_db.*` |
| Regions | Automatic region switching from the disc header (sets the SMPC area code and NTSC/PAL), with a preferred-region order | `Saturn::AutodetectRegion` |
| Cartridges | Backup RAM (4–32 Mbit), 8/32/48 Mbit DRAM, 16 Mbit ROM (KOF95, Ultraman); recommended cartridges inserted automatically per game | `hw/cart/`, `db/game_db.*` |
| Backup memory | Internal and cartridge backup RAM, optionally one internal backup file per game; a manager to import, export, delete and copy saves between internal and cartridge memory | `sys/backup_ram.*`, `ui/windows/backup_ram_manager_window.*` |
| CD block | High-level emulation by default, or **low-level emulation** with a real CD block ROM | `hw/cdblock/`, `hw/sh1/` |
| Recent discs, drag and drop | The last 10 discs; drop a file on the window to load it | `services/disc_service.*` |

## Controls

| Feature | Details |
|---|---|
| Two ports, many peripherals | Control Pad, 3D Control Pad, Arcade Racer, Mission Stick (three-axis and dual), Virtua Gun, Shuttle Mouse |
| Fully rebindable | Up to 5 inputs per action from keyboard (with modifier combos), mouse and gamepads. Deadzones and analog-to-digital sensitivity are adjustable |
| Hotkeys | Pause, frame step, rewind, turbo, save and load state slots, screenshots, full screen, volume, reset, debugger and more (`app/actions.hpp`) |
| Light gun and mouse | System cursor or physical mouse capture per port, with crosshairs |
| Gamepad UI navigation | ImGui gamepad navigation once the UI is focused |

## Saving and time travel

| Feature | Details |
|---|---|
| Save states | 10 slots per game with undo for both saving and loading. States from older versions are migrated (the format is versioned) |
| Rewind | Up to one minute at 60 fps, XOR-delta plus LZ4 compressed on a background thread |
| Frame step | Forward, and backward using the rewind buffer |
| Speed control | Main and alternate speed factors, turbo (hold or toggle), pause, and pause when the window loses focus |

## Video

| Feature | Details |
|---|---|
| Graphics backends | Direct3D 12 (Windows), Metal (macOS), SDL Renderer (fallback everywhere) |
| Hardware-accelerated VDP | VDP1/VDP2 rendering on the GPU with Direct3D 12 compute shaders |
| Software renderer | Accurate CPU renderer with optional VDP1, VDP2 and deinterlacer threads |
| Enhancements | Deinterlaced/progressive rendering of high-resolution interlaced modes; transparent meshes (real half-transparency instead of checkerboard meshes) |
| Scaling | Sharp scaling (integer nearest, then linear), forced integer scaling, aspect ratios (4:3, 3:2, 16:10, 16:9), rotation, auto-fit window |
| Full screen | Exclusive or borderless, display and mode selection, VRR support and low input lag; video sync options; reduced latency mode |
| Screenshots | PNG with configurable scale, written on a background thread |

## Audio

| Feature | Details |
|---|---|
| SCSP emulation | 32 slots, DSP, timers, linear interpolation ("accurate to the hardware") or nearest |
| CD audio | Including MP3/OGG tracks in CUE sheets |
| MIDI | MIDI input and output through RtMidi, including virtual ports |
| Options | Volume and mute, step granularity (sync precision), optional SCSP thread |

## Accuracy and compatibility tweaks

- SH-2 cache emulation (forced on for games that need it).
- SH-2 clock factor ("overclock") from 25 % to 10 000 %.
- CD read speed from 2× to 200×.
- A *Tweaks* settings tab with presets that trade accuracy for speed.
- A per-game database of required cartridges and workarounds (`db/game_db.cpp`).

## Interface

- A Dear ImGui interface with docking, UI scaling (100–200 %), Japanese glyphs and Material Symbols icons.
- On-screen messages, FPS and speed indicators, and a rewind bar.
- Windowed video output, an optional view of the Saturn screen inside an ImGui window.
- Portable or installed profile. Settings are saved automatically to `Ymir.toml`.
- Update checks for the stable and nightly channels (desktop) and Discord Rich Presence (desktop).

## Debugger

A work-in-progress debugger is built into the frontend (see [frontend.md](frontend.md#debugger)):

| Component | Features |
|---|---|
| SH-2 (master and slave) | Disassembly with stepping; breakpoints and watchpoints; registers; data and call stacks; interrupts, exception vectors, cache, DIVU, timers, DMAC with traces |
| SCU | Registers, DMA and interrupt traces, DSP disassembly and data |
| SCSP | Output oscilloscope, slots, key-on trace |
| VDP | VDP1 and VDP2 registers, layer toggles, background, sprite, color calculation and window parameters, debug overlays, VRAM access patterns, editable CRAM |
| CD block | HLE command trace, filters, partitions; LLE drive and YGR traces |
| Memory | 8 memory viewers with optional side effects; full memory dumps; SCU debug port output |

## Platforms

| Platform | Architectures | Status |
|---|---|---|
| Windows 10+ | x86-64 (SSE2 or AVX2), ARM64 | Primary |
| Linux | x86-64, ARM64 | Primary (Ubuntu); others community-supported |
| macOS 12+ | x86-64, ARM64 | Supported (Metal) |
| FreeBSD | x86-64 | Community-supported |
| **UWP: Xbox Series X\|S Dev Mode and Windows** | x86-64 (AVX2) | **This fork**, in bring-up. See [docs/uwp](../uwp/README.md) |
