# Glossary

> Part of the [Ymir documentation](README.md).

| Term | Meaning |
|---|---|
| **A-bus** | The bus behind the SCU that connects the cartridge slot (CS0/CS1) and the CD block (CS2) |
| **Area code** | The SMPC's region value (J, T, U, B, K, A, E, L) that the BIOS compares with the disc's compatible areas |
| **B-bus** | The bus behind the SCU that connects VDP1, VDP2 and the SCSP |
| **Backup RAM (BUP)** | Battery-backed save memory: 32 KiB internal, or a 4–32 Mbit cartridge |
| **C-bus** | The CPU bus: the SH-2s, work RAM, BIOS, SMPC |
| **CDDA** | CD Digital Audio, the music tracks on game discs, streamed by the CD block to the SCSP |
| **CD block** | The Saturn's CD subsystem: an SH-1 CPU, the YGR chip, buffer DRAM and the drive |
| **CHD** | MAME's compressed disc image format |
| **Color calculation** | VDP2 blending between layers (ratio or additive), plus special modes |
| **CRAM** | VDP2 color RAM (4 KiB): palettes in RGB555 or RGB888 |
| **CYCxx** | VDP2 registers that assign VRAM access slots to backgrounds and the CPU |
| **Deinterlace** | Ymir enhancement that renders interlaced high-resolution modes progressively instead of weaving fields |
| **devlog** | Ymir's compile-time-gated developer log (`util/dev_log.hpp`); not the user log |
| **DIVU** | The SH-2's on-chip division unit |
| **DMAC** | The SH-2's on-chip DMA controller (not to be confused with the SCU DMA) |
| **DSP (SCU)** | Fixed-point coprocessor in the SCU, used for 3D math |
| **DSP (SCSP)** | The SCSP's 128-step effects processor |
| **EmuEvent / GUIEvent** | Frontend messages between the UI thread and the emulator thread |
| **FAD** | Frame address: a CD sector index (75 per second, starting at 150 for the first data sector) |
| **FRT** | The SH-2's free-running timer; its input capture is how the two SH-2s signal each other |
| **HLE / LLE** | High-level vs. low-level emulation. Ymir's CD block can be either |
| **INTBACK** | The SMPC command that returns the system status and controller data |
| **IP.BIN** | The system area at the start of a Saturn disc: the `SEGA SEGASATURN` header and boot code |
| **IPL** | Initial Program Loader: the Saturn BIOS ROM (512 KiB) |
| **M68K / MC68EC000** | The sound CPU, running the sound driver in sound RAM |
| **Mesh** | VDP1 checkerboard drawing mode, used to fake transparency; Ymir can render it as real half-transparency |
| **MSH2 / SSH2** | Master and slave SH-2 |
| **NBG / RBG** | VDP2 normal (scrolling) backgrounds 0–3 and rotation backgrounds 0–1 |
| **Peek / Poke** | Side-effect-free memory reads and writes, used by the debugger |
| **Probe** | A component's debugger interface to inspect and modify internal state |
| **Profile** | The folder holding Ymir's settings, ROMs, saves, states and screenshots (portable or user) |
| **Rewind buffer** | One minute of compressed per-frame save states |
| **Scheduler** | The core's event timer: absolute timestamps with a cached nearest deadline |
| **SCSP** | Saturn Custom Sound Processor (Yamaha YMF292): 32 PCM/FM slots, DSP, timers, MIDI |
| **SCU** | System Control Unit: bus bridge, DMA, DSP, interrupt controller, timers |
| **SH-1 / SH-2** | Hitachi SuperH CPUs: SH-1 in the CD block, two SH-2 (SH7604) as main CPUs |
| **Slot** | One SCSP voice |
| **SMEM** | The SMPC's battery-backed memory (4 bytes), set with SETSMEM |
| **SMPC** | System Manager and Peripheral Control: reset, clocks, RTC, controllers, region |
| **Spillover** | Cycles executed past a deadline, carried into the next slice |
| **Tracer** | An interface the core calls on interesting events (instructions, interrupts, DMA) for the debugger |
| **VDP1** | The sprite and polygon processor |
| **VDP2** | The background and compositing processor |
| **VRR** | Variable refresh rate (G-Sync, FreeSync) |
| **WRAM-L / WRAM-H** | Work RAM low (1 MiB DRAM at `0x020'0000`) and high (1 MiB SDRAM at `0x600'0000`) |
| **YGR** | The CD block's gate array that bridges it to the main system |
