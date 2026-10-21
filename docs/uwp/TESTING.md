# Testing Ymir on the Xbox without a mouse

The Ymir frontend is a mouse-and-keyboard application: every menu, dialog and file picker is an ImGui widget. The
Xbox has no mouse, so the ImGui layer needed a gamepad-driven navigation mode — that is now implemented (§5).

This document is the playbook for **testing the emulator itself** on the console, the ways to reach it without a
mouse, and the notes on the gamepad routing between the GUI and the emulated Saturn.

Read [HANDOFF.md](HANDOFF.md) for the current port state.

---

## 1. The key distinction: the emulator is already gamepad-driven

The mouse is **only** needed for the ImGui layer. The emulated Saturn is already driven by the gamepad, through the
default port 1 Control Pad bindings (`apps/ymir-sdl3/src/app/settings.cpp`, `Settings::ResetBinds` for
`input.ports[0].controlPad.binds`):

| Saturn button | Gamepad button |
|---|---|
| A | `X` |
| B | `A` |
| C | `B` |
| X | `LeftBumper` |
| Y | `Y` |
| Z | `RightBumper` |
| L | `LeftTrigger` |
| R | `RightTrigger` |
| Start | `Start` |
| D-pad | D-pad **and** left stick |

So once a game is running, the console is enough to play it. What is missing is a way to *reach* that state without
clicking.

Three levers, in increasing order of work:

| Lever | What it solves | Work |
|---|---|---|
| **A. Drop the BIOS in place** | Removes the *Welcome* modal, which is the only modal that blocks the boot | Copy one file |
| **B. Pre-seed the recent-discs list** | Removes the need for the file picker to load a game | Copy two files, edit one line |
| **C. Bind a gamepad button to a hotkey** | Full screen, pause, reset, screenshots, without the GUI | Edit one line in `Ymir.toml` |
| **D. Gamepad navigation in the ImGui layer** | Makes the whole ImGui layer navigable | **Implemented** (see §5) |

Levers A, B and C need **no code change at all**.

---

## 2. Lever A — the BIOS (IPL ROM) is auto-detected

`App::Run` calls `m_romService.ScanIPLROMs()` and `m_romService.LoadIPLROM()` during boot. If no ROM is found the app
opens the *Welcome* modal instead (`app.cpp`, `OpenWelcomeModal(true)`), which is the modal that blocks the flow.
**With a ROM present, that modal never opens.**

Copy the BIOS to:

```
<profile>/roms/ipl/
```

On the Xbox the profile root is:

```
Q:\Users\UserMgr2\AppData\Local\Packages\Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm\LocalState\StrikerX3\Ymir\
```

i.e. in the Device Portal file browser:

```
LocalAppData\Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm\LocalState\StrikerX3\Ymir\roms\ipl\
```

Any of the usual BIOS image names works — the scan looks at the file contents, not the name.

> The scan happens **once, at boot**. Copying the ROM while the app is running does nothing; restart the app.

## 3. Lever B — load the disc without the file picker

`App::Run` has three branches for the disc (`app.cpp`):

```cpp
if (!options.gameDiscPath.empty()) {                                   // command line
    m_discService.LoadDiscImage(options.gameDiscPath, false);
} else if (settings.general.rememberLastLoadedDisc && !m_context.state.recentDiscs.empty()) {
    m_discService.LoadDiscImage(m_context.state.recentDiscs[0], false); // <-- this one
} else {
    m_context.EnqueueEvent(events::emu::InsertCartridgeFromSettings());
}
```

The middle branch is the GUI-free path. It needs two things:

1. **`Ymir.toml`**, in the profile root, with:

   ```toml
   [General]
   RememberLastLoadedDisc = true
   ```

   The key is `false` by default (`Settings::Settings`, `general.rememberLastLoadedDisc = false`).

2. **`state/recent_discs.txt`**, in the profile root, with the absolute path of the disc image, one path per line:

   ```
   Q:\Users\UserMgr2\AppData\Local\Packages\Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm\LocalState\StrikerX3\Ymir\games\Game.cue
   ```

   The file is plain text, UTF-8, one path per line (`DiscService::LoadRecentDiscs` / `SaveRecentDiscs`). The most
   recent disc is the first line.

Copy the disc image (`.cue` + `.bin`/`.iso`/`.ccd` — the whole set) next to the profile, for example in a `games`
folder, and point `recent_discs.txt` at the `.cue`.

The app then loads the disc and the BIOS by itself, and the emulator starts. **No mouse needed.**

## 4. Lever C — bind gamepad buttons to hotkeys

Every general action in `Ymir.toml` is a list of bind strings. A gamepad button is written as
`Gamepad<Button>@<player index>` (`input::ToString(const InputElement &)` in `input_events.cpp`).

To toggle full screen with the Guide button, add to the top-level `[Hotkeys]` section:

```toml
[Hotkeys]
ToggleFullScreen = ["GamepadGuide@0"]
```

Useful actions to bind this way (`app/actions.hpp`):

| Action | TOML key | What it does |
|---|---|---|
| Toggle full screen | `ToggleFullScreen` | Fills the screen with the video, hiding the ImGui chrome |
| Toggle windowed video output | `ToggleWindowedVideoOutput` | Moves the video out of the ImGui window |
| Pause / resume | `PauseResume` | Freezes the emulator |
| Soft reset | `SoftReset` | Resets the Saturn |
| Hard reset | `HardReset` | Full reboot |
| Toggle mute | `ToggleMute` | Mutes the XAudio2 output |
| Take screenshot | `TakeScreenshot` | Saves a PNG under `<profile>/screenshots/` |
| Toggle frame rate OSD | `ToggleFrameRateOSD` | Shows the FPS overlay |
| Exit | `ExitApp` | Quits |

The player index is the one assigned by `App::Run` when the gamepad connects: the first gamepad is `0`.

> The app rewrites `Ymir.toml` when it exits. Edit the file while the app is **not** running, otherwise the change is
> lost.

## 5. Lever D — gamepad navigation in the ImGui layer (implemented)

This is the one that needed code, and it is now in the tree.

### 5.1 Why the ImGui layer was unreachable

The app enabled ImGui's gamepad navigation conditionally on `io.NavActive`:

```cpp
if (io.NavActive) {
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
} else {
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
}
```

That is a logical deadlock. In ImGui, `io.NavActive` is computed as
`nav_keyboard_active || nav_gamepad_active`, and `nav_gamepad_active` requires `NavEnableGamepad` to already be set.
On the PC the keyboard breaks the tie, so `io.NavActive` becomes true and the gamepad flag is enabled. On the Xbox
there is no keyboard, so neither term could ever become true: the GUI was permanently unreachable with the
controller.

ImGui 1.93 already ships the whole console navigation model, and the vendored `imgui_impl_sdl3` backend already feeds
it. Nothing was missing except the flag.

| Input | ImGui action |
|---|---|
| D-pad / left stick | Move the focus between widgets |
| `A` (south) | Activate the focused widget |
| `B` (east) | Go back / cancel |
| `X` (west) | Toggle the menu layer |
| `Y` (north) | Open the context menu |
| `L1` / `R1` | Slow / fast navigation tweak |

### 5.2 The routing rule: one gamepad, two consumers

The Xbox has a single gamepad that has to serve both the GUI and the emulated Saturn. If both consumed the same
button press, every click in a menu would also press a button in the game.

The rule implemented in `app.cpp` is:

- **A modal dialog is open** → the gamepad drives the GUI. The Saturn gets nothing. This is what makes the *Welcome*
  modal dismissible with the controller.
- **Otherwise** → the gamepad drives the Saturn, and the GUI ignores it.
- **The View button** (`Back` in the SDL naming) toggles the GUI focus manually, for the moments when there is no modal
  to grab the gamepad but you still want to reach a menu.

The modal detection uses `io.WantCaptureKeyboard`, which is only meaningful because the UWP build turns the keyboard
navigation off: with no keyboard attached, ImGui reports `WantCaptureKeyboard` exactly while a modal is open or a
widget is being interacted with.

A small overlay in the top-right corner shows which side currently has the gamepad: *Gamepad: Emulator*,
*Gamepad: GUI* or *Gamepad: GUI (dialog)*.

### 5.3 The virtual cursor (not implemented — probably not needed)

The original idea for this lever was a virtual mouse cursor driven by the analog stick. It is **not** implemented, and
it is most likely unnecessary: ImGui's own gamepad navigation is the native console way of driving a widget list, it
covers the file picker's buttons and the *Welcome* modal, and it does not need a second, parallel mouse model.

If it is ever needed, the design is:

**Where it goes.** `apps/ymir-sdl3/src/app/app.cpp` funnels every SDL event through `ImGui_ImplSDL3_ProcessEvent(&evt)`
before dispatching it to the input context. ImGui therefore consumes SDL mouse events directly, and a virtual cursor
must arrive as `SDL_EVENT_MOUSE_MOTION` / `SDL_EVENT_MOUSE_BUTTON_DOWN` / `SDL_EVENT_MOUSE_BUTTON_UP` events to be
seen by ImGui.

**How.** When the left stick is pushed past its deadzone, push a synthetic mouse-motion event per frame with the
accumulated delta, and clamp the resulting position to the window. A/D (or the right stick) maps to left/right mouse
button, and the right stick or the triggers act as a virtual scroll wheel.

**Why synthetic events instead of talking to ImGui directly.** Everything that consumes the mouse — the ImGui layer,
the mouse capture service, the light gun and shuttle mouse peripherals — already speaks SDL mouse events. Injecting
at that level keeps the feature coherent instead of adding a second, parallel mouse model.

**Why not SDL's own machinery.** SDL 3 has no built-in gamepad-to-cursor feature. It would have to be implemented
inside `vendor/SDL3-uwp`, which the port rules forbid.

**Open questions.**

- Does the virtual cursor fight the *real* mouse on the PC build? It should be off by default there.
- Should the emulator's own mouse input (light gun) also be driven by it? Probably not — the gamepad already drives
  the Saturn's D-pad.
- Is the stick-as-cursor an absolute or a relative mapping? Relative (like a trackpad) is the usual choice for ImGui,
  but a hard "the cursor is at the edge" feel is easier to control.
- Speed. A per-frame delta needs a gain factor, and probably an acceleration curve.

**Alternative worth considering first.** The Xbox controller supports a mouse-like pointer on the desktop shell, but
not inside a UWP app. If a future SDL3-uwp exposes a virtual mouse device, the whole feature collapses to "open that
device and feed it the stick".

## 6. The file picker on the Xbox

The file picker is the WinRT one (`FileOpenPicker` / `FileSavePicker` / `FolderPicker`), implemented in
`apps/ymir-sdl3/src/platform/uwp/uwp_platform.cpp`. SDL3-uwp has no file dialog in its CMake build, so the port
provides its own.

**It should open.** The picker is a system UI on the Xbox, so it is navigable with the controller — no mouse needed.
The `FolderPicker` is the restricted one on Xbox (it cannot be used to browse the console's internal storage); the
`FileOpenPicker` is not restricted that way.

**The filters degrade to "all files".** WinRT's `FileTypeFilter` is a flat list of extensions with no named groups,
unlike the desktop SDL dialog. The current implementation collects the extensions of every filter, but as soon as one
filter is the `*` wildcard (every dialog in Ymir has an "All files" entry) the list is reduced to `*`. The practical
result on the Xbox is that the picker shows **all files** — which is the friendlier behaviour on a console, where
browsing a file system with a controller is the normal way to do it. The app validates the selection afterwards and
shows an error modal if the file is not a valid image.

**The real risk: reading the file afterwards.** The picker returns a `StorageFile`, and the app takes its `Path()` and
opens it **by path**. Files inside the app's own `LocalState` always work. Files picked from the console's internal
storage or from a USB drive depend on the app container being allowed to open that path: the `broadFileSystemAccess`
capability is documented as *not supported on Xbox*, so the raw path open may fail even for a file the user just
picked. This is the one thing about the picker that is **not** verified yet on real hardware.

If the raw open fails, the fix is not to fight the capability system but to copy the picked file into the app's
`LocalState` and load it from there — the picker hands out a `StorageFile`, which can be read with WinRT APIs that do
not need raw path access.

**Consequence for testing:** the levers in §2 and §3 (drop the BIOS in `roms/ipl/`, pre-seed `state/recent_discs.txt`)
do not depend on the picker at all, and are the reliable path to test the emulation.

## 7. Copying files to the Xbox

| Method | Notes |
|---|---|
| **Device Portal** (Windows) | `xbox.local` → *File Explorer* → `LocalAppData\<PFN>\LocalState\StrikerX3\Ymir\`. Best for one-off copies. |
| **Xbox Manager / Storage** | Mounts the console's storage on the PC. |
| **USB drive** | The manifest has the `removableStorage` capability, so a drive can be used for the BIOS and games. |

The PFN is `Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm` unless the publisher changes.

## 8. Grabbing the log

The app writes stdout and stderr to:

```
<profile>/ymir-uwp.log
```

It is flushed line by line. When a debugger is attached, the same lines also go to `OutputDebugStringA`, so they show
up in the Visual Studio Output window (that is the `ymir-sdl3` log tee in `platform/uwp/uwp_platform.cpp`).
