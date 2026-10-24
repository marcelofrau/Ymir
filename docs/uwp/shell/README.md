# The Xbox shell — a gamepad-first interface themed after the Saturn BIOS

The shell is the user interface of the Xbox build. It replaces the desktop ImGui interface (menu bar plus windows)
with a full-screen, controller-only interface inspired by the Sega Saturn BIOS: a starfield in "warp", a spaceship
console panel and spheres as menu options. The desktop interface stays reachable as an advanced mode.

- Phases and status: [PLAN.md](PLAN.md)
- Art asset contract (what to draw, file names, sizes): [ASSETS.md](ASSETS.md)
- How to test it on the console: [../TESTING.md](../TESTING.md) §10

The shell only exists in the UWP build. Everything in this document is compiled under `YMIR_PLATFORM_UWP`.

## 1. Goals and constraints

| Goal / constraint | Consequence |
|---|---|
| Controller only, no mouse, no keyboard | Every screen is reachable with the d-pad, A and B. No virtual cursor in the shell |
| Readable on a TV | Layout designed against a 1920x1080 reference canvas, scaled to the real display, with a TV safe area |
| Themed after the Saturn BIOS, artwork still being made | Art comes from a manifest; every asset has a procedural placeholder so the shell works without artwork |
| This checkout is a fork that rebases on upstream | The shell lives in port-owned directories and touches shared code only through one-line guarded hooks |
| The desktop interface must keep working, on the console too | The shell is a mode, not a rewrite. The desktop interface, its virtual cursor and its L3/R3/View routing are untouched |

## 2. Where the code lives

All of it is new and sits in port-owned directories:

| Path | Role |
|---|---|
| `apps/ymir-sdl3/src/platform/uwp/shell/shell.{hpp,cpp}` | The facade owned by `app::App`: interface mode, screen state, gamepad routing, emulation pause/resume, actions |
| `.../shell/shell_config.{hpp,cpp}` | `<profile>/shell.toml` (mode, safe area) |
| `.../shell/layout.{hpp,cpp}` | Reference canvas → display mapping, letterbox, safe area. No ImGui dependency, unit tested |
| `.../shell/starfield.{hpp,cpp}` | Procedural warp starfield simulation and projection. No ImGui dependency, unit tested |
| `.../shell/theme.{hpp,cpp}` | `theme.toml` manifest, PNG decoding, GUI textures, sprite sheets, 9-slice panels |
| `.../shell/theme_layout.hpp` | Positions tied to the artwork (`[layout]` of `theme.toml`) |
| `.../shell/dot_matrix.{hpp,cpp}` | 5x7 dot-matrix font (printable ASCII and a music note) and UTF-8 decoding. No ImGui dependency, unit tested |
| `.../shell/marquee.{hpp,cpp}` | The scrolling message queue of the LCD. No ImGui dependency, unit tested |
| `.../shell/quad_mesh.{hpp,cpp}` | Cube/sphere quad mesh, projection, culling and posterized flat lighting. No ImGui dependency, unit tested |
| `.../shell/widgets.{hpp,cpp}` | Backdrop, dashboards and LCD, scanlines, panels, vector and dot-matrix text, controller glyphs and legend, flat-shaded meshes, sphere button |
| `.../shell/screens/home_screen.{hpp,cpp}` | The home screen: library grid (all games / recent tabs), action spheres on the cockpit, missing BIOS panel |
| `.../shell/overlay.{hpp,cpp}` | Overlay windows: a stack of screens with LB/RB pages of declarative rows (action, toggle, choice, slider, info) |
| `.../shell/settings_pages.cpp` | The settings screen (System, Video, Audio, Controls, Library, Interface, Advanced, About) and the pause menu |
| `.../shell/game_library.{hpp,cpp}` | Library folders scanned on demand in the background; headers cached in `<profile>/state/shell_library.toml` |
| `.../shell/cover_cache.{hpp,cpp}` | Covers from `<profile>/covers/`, loaded on demand and scaled down |
| `.../shell/music_player.{hpp,cpp}` | Background music from `<profile>/music/` on a second XAudio2 output, with fades and level metering |
| `.../shell/audio_decoders.{hpp,cpp}` | OGG (stb_vorbis), MP3 (dr_mp3), FLAC (dr_flac) and WAV (dr_wav) decoding from memory |
| `.../shell/audio_cube.{hpp,cpp}` | Envelope and motion of the audio-reactive cubes. No ImGui dependency, unit tested |
| `.../shell/file_system.{hpp,cpp}` | Directory listing and file reading through the brokered `...FromApp` Win32 API (USB drives, picked folders) |
| `.../shell/shell_log.hpp` | The `UWP-Shell` dev log group |
| `apps/ymir-sdl3/uwp/shell/assets/` | Embedded artwork and `theme.toml` |
| `apps/ymir-sdl3/uwp/ShellAssets.cmake` | Embeds every `.toml`/`.png` under `uwp/shell/assets/` as the cmrc library `ymir-sdl3-shell-rc` |
| `tests/ymir-sdl3-tests/src/platform/uwp/shell/` | Catch2 tests of the layout, starfield, marquee, quad mesh and dot-matrix font, run by the desktop build |

## 3. Hooks in shared code

Every hook is inside `#if YMIR_PLATFORM_UWP` (or the UWP-only blocks the fork already had) and is one to three lines.

| File | Hook |
|---|---|
| `apps/ymir-sdl3/CMakeLists.txt` | Shell sources in the existing UWP `target_sources` block, link to `ymir-sdl3::shell-rc`, `include(uwp/ShellAssets.cmake)` |
| `app/app.hpp` | `platform::uwp::shell::Shell m_shell{m_context}`; `GamepadDrivesGui()` asks the shell when it is active |
| `app/app.cpp`, `App::Run()` | `m_shell.LoadConfig()` before the IPL ROM scan; the Welcome modal is not opened while the shell is active (the shell has its own missing BIOS panel) |
| `app/app.cpp`, after the logo texture | `m_shell.Init()` (theme textures) |
| `app/app.cpp`, gamepad button event | `m_shell.OnGamepadButton(...)` first; the desktop L3/R3/View focus toggles and the RB/LB/X/Y shortcuts only apply when the shell is not active |
| `app/app.cpp`, `UpdateVirtualMouse()` and mouse auto-hide | No virtual cursor while the shell is active |
| `app/app.cpp`, `drawMainMenu` | No menu bar while the shell is active |
| `app/app.cpp`, before `DrawWindows()` | `m_shell.Draw()` |
| `app/app.cpp`, gamepad focus indicator | Hidden while the shell is active |
| `app/app.cpp`, `LoadDisc` handler (emulator thread) | `m_shell.OnDiscLoaded()` |

Only two of those touch upstream lines (the Welcome modal call is re-indented under an `if`, and `drawMainMenu`
gets an early return); the rest are inside code the fork already owns.

## 4. How it works

### 4.1 Frame

`Shell::Draw()` runs every frame after `ImGui::NewFrame()` and before the desktop windows. On the home screen it opens
a full-screen ImGui window (`##ymir_shell`, `NoBringToFrontOnFocus`, no background) and draws, in order: the space
backdrop and starfield, the screen contents, and the footer hints. Because desktop windows and the generic modal are
drawn after it, anything the shell opens from the desktop layer (the Settings window, error dialogs) sits on top.

In game the shell draws nothing except a "VIEW · Ymir menu" hint that fades out five seconds after the game starts.

### 4.1.1 Visual language

A 90s console BIOS with a high-tech finish, keeping the Saturn's 32-bit identity:

| Element | Look | Why |
|---|---|---|
| Backdrop | `bg_space` artwork (stars and galaxy) with the procedural starfield over it: faint square stars while cruising, streaks during warp jumps | The artwork carries the detail; the procedural layer adds motion without cluttering it |
| Dashboards | `dash_top` and `dash_bottom` artwork framing a window of sky | The cockpit of the Saturn BIOS |
| Top bar | A **dot-matrix LCD**: a grid of unlit dots, "YMIR" at double size, the version, a divider and the **marquee**, which moves one whole dot column at a time | Reads like the character LCD of 90s hardware |
| Marquee | App messages first (the same `SharedContext::messages` the desktop toasts use: resets, disc and BIOS loads), then rotating idle texts (what is in the tray, tips) | One place for status without popping toasts over the art |
| Spheres | Faceted **quad meshes** (a subdivided cube morphed into a sphere), flat-shaded with a posterized light ramp and thin polygon edges, slowly turning and spinning up when focused | The VDP1 draws quads; flat-shaded quad geometry is the Saturn look |
| Focus halo | A ring in the Saturn's **"mesh" transparency** (a checkerboard of dots instead of alpha blending) plus orbiting pixel blocks | Mesh transparency is the most recognizable Saturn rendering trick |
| Option name | Display font with a slight cyan/magenta convergence split | High-tech, CRT-like |
| Descriptions, legend, glyph letters | Dot-matrix text | Same voice as the LCD |
| Whole screen | A faint scanline pass on top | CRT finish |

### 4.1.2 Home screen

- The **library grid** floats over the sky as hologram tiles (translucent panel, corner brackets, drifting
  holographic scanlines, a faint flicker); the stars and the galaxy stay visible behind it. The focused tile lifts,
  glows and shows its cover in full color. Tabs **All games** / **Recent** switch with LB/RB.
- The **action spheres** (Start/Resume, Load Disc, Settings, Desktop UI, Quit) sit on the cockpit console.
- The **cubes** float over the two pods, opaque, each following one channel of the background music.
- The name and details of the focused item (game title, product number, region, release date) show above the grid.

### 4.1.3 Library, covers and music

| What | Where | How |
|---|---|---|
| Library folders | `[Library] Folders` in `shell.toml` | Added with the folder picker (X on the home screen, or Settings > Library). Removed in Settings > Library |
| Scan | On demand only: after adding a folder, with Y (Rescan) or from Settings | Background thread; `.cue/.chd/.iso/.ccd/.mds` up to 4 levels deep; header read with `ymir::media::LoadDisc`; unchanged files (same size and modification time) reuse the cache |
| Cache | `<profile>/state/shell_library.toml` | Loaded at startup, so the grid is there without scanning |
| Covers | `<profile>/covers/<product number>.png/jpg` or `<disc file name>.png/jpg` | Loaded two per frame at most, scaled down to 256 px |
| Music | `<profile>/music/*.ogg/mp3/flac/wav` | Never shipped; the user's own files. Fades in on the shell screens, out in game |

The Shell file and folder browser lists drives and directories on a background worker. While removable storage is being
checked, the picker stays visible with a loading row; closing it does not block the render thread.

Files outside the package (a USB drive, a picked folder) are only reachable through the brokered `...FromApp` Win32
functions, so the library, the covers and the music go through `fs::ListDirectory` / `fs::ReadWholeFile`.

### 4.1.4 Overlays: settings and pause menu

`Overlay` is a stack of screens drawn as a hologram panel in their own ImGui window, over the shell window and under
the desktop windows. Rows are data with getter/setter callbacks:

| Input | Effect |
|---|---|
| D-pad up/down | Move between rows |
| A | Run an action, flip a toggle, step a choice |
| D-pad left/right | Step a choice or a slider, flip a toggle |
| LB / RB | Previous / next page |
| B | Back to the previous screen; closes the overlay from the first one |

Every row writes the same `Settings` fields and sends the same events as the matching desktop widget, so the two
interfaces stay consistent. Pages: System, Video, Audio (game and background music), Controls (peripheral per port,
button mapping through the desktop binds editor, dead zones), Library, Interface (safe area, scanlines, desktop
mode), Advanced (backup memory manager and the desktop settings tabs the shell has no page for) and About.

In game, **View** opens the **pause menu** over the frozen game: resume, save state slot with save/load, reset, hard
reset, change disc, settings, back to the library, desktop interface.

### 4.2 Navigation

The widgets are ordinary ImGui items: each sphere is an `InvisibleButton` drawn by hand. ImGui's own gamepad
navigation moves the focus with the d-pad and the left stick and activates with A, including the 2D "nearest item in
that direction" logic. The shell only adds:
- focusing the first option when a screen is shown (`SetKeyboardFocusHere`), and re-focusing the shell window if no
  window has the focus (for instance after the desktop Settings window closes);
- B as "back to the game", only while the shell window has the focus, so B still closes a desktop window or dialog
  opened on top;
- hiding ImGui's rectangular navigation cursor (`ImGuiCol_NavCursor` transparent), since each widget draws its own
  focus effect.

ImGui sees the gamepad buttons regardless of `ImGuiConfigFlags_NavEnableGamepad` (the SDL3 backend polls them every
frame); the flag only decides whether they move the focus. The app sets the flag from `GamepadDrivesGui()`, which is
`m_shell.OwnsGamepad() || modal open` while the shell is active.

### 4.3 Gamepad routing

| State | Gamepad goes to | View | Emulation |
|---|---|---|---|
| Shell, home screen | Shell (ImGui navigation) | Back to the game, if one was started | Paused by the shell |
| Shell, in game | Emulated Saturn | Opens the shell | Running |
| Any modal open | The modal | — | Unchanged |
| Desktop mode | As before (L3/R3/View toggle, virtual cursor) | Hold 1 s → back to the shell | Unchanged |

The Saturn control pad has no button for View, so using it as the shell button costs the game nothing. On every
handover the shell calls `InputContext::ResetAllGamepadInputs()`, so a button held at the moment of the switch
does not stick on the Saturn.

The shell only pauses the emulation if it was running, and only resumes it if it paused it, so a pause the user set
in the desktop interface survives a trip through the shell.

### 4.4 Commands

The shell does not reimplement anything; it calls the same events and services the desktop interface uses:

| Action | Implementation |
|---|---|
| Start / resume | `events::emu::SetPaused(false)` |
| Load disc | `DiscService::OpenLoadDiscDialog()`; the emulator thread's `LoadDisc` handler calls `Shell::OnDiscLoaded()`, which the GUI thread picks up on the next frame through an atomic flag |
| Select BIOS | The Shell file browser selects and copies the IPL ROM into the profile, then uses `util::LoadIPLROM`, `settings.system.ipl.{overrideImage,path}`, `ROMService::LoadIPLROM()` and `events::emu::HardReset()` |
| Scan again | `ROMService::ScanIPLROMs()` + `LoadIPLROM()` + hard reset |
| Settings (until phase 3) | Opens the desktop `SettingsWindow` on top of the shell |
| Desktop UI | Writes `Mode = "Desktop"` to `shell.toml` and lets the emulation run |
| Quit | Pushes `SDL_EVENT_QUIT` |

### 4.5 Art assets

`Theme::Load()` reads `theme.toml` and every PNG it lists, decodes them with stb_image and creates GUI textures
through `GraphicsService::CreateTexture()` — the same path as the Ymir logo. The decoded pixels are kept for the
lifetime of the app because the graphics service re-uploads every texture when the graphics backend is recreated.

Every file is looked up in `<profile>/shell-theme/` first, then in the embedded copy. Missing assets make `Find()`
return `nullptr` and the widget draws a procedural placeholder. See [ASSETS.md](ASSETS.md).

## 5. Decisions

| # | Decision | Why | Rejected alternative |
|---|---|---|---|
| S1 | A separate interface (the shell) instead of adapting the desktop one | The desktop interface is a menu bar plus windows designed for a mouse; making it TV- and controller-friendly would mean editing the most upstream-active code in the repository (`app.cpp` gets ~15 upstream commits a month, `ui/` ~18) | Restyling the desktop interface in place |
| S2 | Own small widget layer, inspired by PCSX2/XBSX2's FullscreenUI but not ported from it | The look is custom (spheres, console panel, starfield); `ImGuiFullscreen` (~126 KB) brings its own visual language, a texture cache, `SmallString`/`GSTexture`/`Host::` dependencies, and would be fought against rather than used. Its *concepts* are kept: reference-canvas layout, ImGui navigation over hand-drawn items, footer glyphs, overlay pause menu. License would have allowed it (GPL-3.0+ into GPL-3.0) | Porting `ImGuiFullscreen.cpp` |
| S3 | ImGui's native gamepad navigation, no focus system of its own | ImGui 1.93 already does directional 2D navigation, scrolling to the focused item and activation; the desktop port already relies on it (port decision D22) | A custom focus graph |
| S4 | Shell settings in its own `shell.toml` | `settings.{hpp,cpp}` is busy upstream; a separate file keeps the shell out of it entirely | New fields in `app::Settings` |
| S5 | Art in a manifest + override folder + procedural placeholders | The artwork is still being made; the shell must work and be testable without it, and art iterations must not need a rebuild | Hard-coded file names, rebuild for every art change |
| S6 | View is the shell button in game | Not part of the Saturn control pad, so no game input is lost; matches what the desktop routing already uses | L3+R3 (XBSX2's default; awkward and used by the desktop routing for the focus toggle) |
| S7 | Pure logic (layout, starfield) without ImGui/WinRT | Lets the desktop `ymir-sdl3-tests` cover it; the UWP build has no tests | Testing only on the console |
| S8 | The shell pauses the emulation on its own screens | The BIOS boot jingle or a game running behind the menu is noise, and a game must not advance while the player is in the menu | Keep running behind the shell |
| S9 | Settings open the desktop Settings window until phase 3 | Gets every setting reachable on day one; the desktop window is gamepad-navigable | Blocking phase 0 on the settings pages |
| S10 | Artwork as layers, positions as data (`[layout]` in `theme.toml`), marquee fed by `SharedContext::messages` | Text and cubes must line up with the art, and art changes must not need a rebuild. Reusing the app's message buffer shows exactly what the desktop toasts would, without new plumbing | Hard-coded coordinates; a separate message channel for the shell |
| S11 | Own 5x7 dot-matrix font drawn as rectangles, instead of a pixel TTF | No font file or license to ship, crisp at any scale (dots snap to pixels), and the unlit-dot grid is what makes the bar read as an LCD | Embedding a third-party pixel font |
| S12 | Procedural faceted quad spheres (and, later, cubes) from one mesh module | The Saturn look comes from flat-shaded quads; one mesh serves the spheres now and the audio-reactive cube/sphere morph of phase A | Pre-rendered sphere sprites only |
| S13 | Background music on a second `app::XAudio2Backend` instance, decoding into a ring buffer on its own thread | The emulator's `AudioSystem` stays untouched; the shell pauses the emulation on its screens, so the two outputs never play together. Decoding from memory avoids any file access from the decoders | Mixing the music into `AudioSystem` |
| S14 | Library scanned only on request, with the folder picker | The user decides what is scanned; enumerating a USB drive is slow and is not done behind the user's back. The cache makes the grid instant on later launches | Scanning drives automatically at startup |
| S15 | One declarative overlay system for every shell window (settings, pause, future tools) | New pages are lists of rows, not new drawing code; input handling is the same everywhere | One hand-made screen per window |
| S16 | Reuse libchdr's dr_flac implementation | libchdr already compiles dr_flac 0.13.4 (the same version as `vendor/dr_libs`, identical structs); compiling a second copy failed with duplicate symbols | A second `DR_FLAC_IMPLEMENTATION` |
| S17 | Enumerate picker directories off the render thread | USB and other brokered storage can respond slowly; a dedicated worker keeps navigation and the loading screen responsive, and shared result state remains safe if the picker closes | Synchronous enumeration in the navigation callback |

## 6. Known limitations

- No pause overlay yet: View in game goes back to the home screen (phase 1 adds the overlay with save states).
- No game library yet: Load Disc opens the system file picker (phase 2).
- Settings use the desktop window (phase 3).
- Error dialogs raised by services use the desktop modal style.
- The shell has not run on the console yet; the layout values match the artwork but have only been checked in an
  offline preview, not on a TV.
- Nothing of phases A, L and O has run on the console yet.
