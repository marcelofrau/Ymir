# Xbox shell — plan

Architecture and decisions: [README.md](README.md). Art assets: [ASSETS.md](ASSETS.md).

Each phase ends with a UWP build, a run on the console by the user (the agent never deploys), and a commit when the
user asks for it. Mark `[x]` when a step is done and validated.

## Phase 0 — skeleton and home screen

- [x] Module under `src/platform/uwp/shell/`, CMake wiring, embedded `theme.toml`
- [x] Hooks in `app.hpp` / `app.cpp` (all guarded), `shell.toml` with the interface mode
- [x] Layout against the 1920x1080 reference with safe area; unit tests
- [x] Procedural warp starfield; unit tests
- [x] Theme loader: manifest, override folder, sprite sheets, 9-slice, procedural placeholders
- [x] Home screen: Start/Resume, Load Disc, Settings (desktop window), Desktop UI, Quit
- [x] Missing BIOS panel: Select BIOS, Scan Again
- [x] View toggles game ↔ shell; hold View 1 s in desktop mode returns to the shell
- [x] UWP Release build green
- [x] Desktop regression (build + ctest) green (26/26)
- [ ] **Console run**: home screen visible with the starfield, spheres navigable with the d-pad, Load Disc → game
      runs, View → home, B → resume, Desktop UI and back

## Phase V — the visual composition (mockup)

- [x] Embedded artwork: `bg_space`, `dash_top`, `dash_bottom` (from `assets/`)
- [x] `[layout]` anchors in `theme.toml`, parsed by the theme
- [x] Starfield over the artwork (faint when cruising, full during warp)
- [x] Top bar as a dot-matrix LCD: "YMIR", version, divider, marquee fed by the app messages and idle texts
- [x] Controller legend on the cockpit floor, per screen
- [x] Faceted flat-shaded quad spheres, mesh-transparency halo, holographic option name, scanlines
- [x] Unit tests: dot-matrix font, marquee, quad mesh
- [x] UWP Release build green; frontend tests 28/28
- [ ] **Console run**: the layers line up with the art on a TV, the LCD reads well, the marquee shows a message after
      loading a disc, the spheres spin and highlight

## Phase A — background music and audio-reactive cubes

- [x] `MusicPlayer`: second `app::XAudio2Backend` instance, decoder thread, OGG (stb_vorbis), MP3 (dr_mp3), FLAC
      (libchdr's dr_flac), WAV (dr_wav); tracks from `<profile>/music/` only, never in the repository
- [x] Fade in on the shell screens, fade out when the game starts; `[Music]` in `shell.toml`; "♪ track" on the marquee
- [x] Cubes over the pods (opaque, floating), left = left channel in pastel green, right = right channel in pastel
      red, morphing towards a sphere and deforming with the level (RMS + low-pass), plain cubes in silence
- [x] Unit tests: envelope, cube motion, mesh deformation
- [ ] Console run with the user's tracks

## Phase L — library grid (replaces phase 2)

- [x] Folder picker to add library folders (X, or Settings > Library); no automatic scan
- [x] Background scan with header cache; covers by product number or file name
- [x] Hologram grid over the sky with All / Recent tabs (LB/RB); action spheres moved to the cockpit console
- [ ] Console run: a folder on `E:\` is scanned and a game starts from the grid

## Phase O — overlays: settings and pause menu (replaces phases 1 and 3)

- [x] Declarative overlay system (pages of rows, LB/RB, A, left/right, B)
- [x] Settings: System, Video, Audio, Controls, Library, Interface, Advanced (desktop tools), About
- [x] Pause menu on View in game: resume, save/load state slots, reset, hard reset, change disc, settings, library,
      desktop interface
- [x] Xbox-style controller glyphs in the legend
- [ ] Console run

## Phase 1 — pause overlay

- [ ] View in game opens a themed overlay over the paused game instead of the full home screen
- [ ] Resume, Save state / Load state by slot (`SaveStateService::List()` / `SaveSaveStateSlot` / `LoadSaveStateSlot`),
      Reset (soft / hard), Change disc, Settings, Home
- [ ] Console run

## Phase 2 — game library grid

- [ ] Library folders in `shell.toml`, added with the folder picker
- [ ] Background scan (`std::jthread` member), disc header via `ymir::media::LoadDisc` + `SaturnHeader`, cache in
      `<profile>/state/shell_library.toml` keyed by path + modification time
- [ ] Covers from `<profile>/covers/<productCode>.png|jpg`, placeholder otherwise
- [ ] Grid screen with title and product code, "Recent" row from `ctx.state.recentDiscs`; A starts the game
- [ ] Home "Games" sphere opens the grid
- [ ] Console run: a folder on `E:\` is scanned and a game starts with the controller only

## Phase 3 — settings overlays

- [ ] Tabs (LB/RB): System (IPL ROM, region), Video (hardware renderer, aspect), Audio (volume, mute), Input
      (minimal bindings, link to the desktop editor)
- [ ] Rows implemented against `Settings` + events, using `ui/widgets/settings_widgets.cpp` and
      `system_widgets.cpp` as the specification
- [ ] Console run: changing the IPL ROM and the hardware renderer from the shell survives a restart

## After the MVP

- Sounds in the style of the BIOS
- Themed error dialogs instead of the desktop modal
- Virtual keyboard (`CoreInputView`) for text fields
- Screen transitions; final artwork for every screen
