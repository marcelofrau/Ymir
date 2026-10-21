# AGENTS.md

Ymir is a Sega Saturn emulator (C++20, CMake). **This checkout is the `uwp/main` fork (`marcelofrau/Ymir`), whose purpose is the UWP / Xbox Dev Mode port.** Work here is almost always about that port, not about upstream emulation features.

## Read before working on the port

- `docs/uwp/README.md` — architecture, every design decision with its rationale, full map of the fork's delta. **Read this before touching port code.**
- `docs/uwp/HANDOFF.md` — **the shared memory between the agents working on this port.** The `START HERE` block at the top is the canonical current state and is rewritten every session; the rest is the sequential Xbox runbook (build → sideload → verify checklist → troubleshooting T-01..T-18) plus the append-only session log. Section 0 lists the hard rules for the executor.
- `docs/uwp/TESTING.md` — how to test on the Xbox without a mouse (IPL ROM and disc auto-load, hotkey bindings, the gamepad-cursor feature design).
- `docs/uwp/evidence/` — raw logs of the Xbox runs and what each one proves or does not prove.
- `CONTRIBUTING.md` — coding, naming and architectural rules for Ymir itself.

## Port-specific rules (from HANDOFF §0 — these override convenience)

1. **Do not refactor or "improve" working code.** The goal is validating on Xbox and fixing only what breaks.
2. **Any change to shared code** (outside `apps/ymir-sdl3/src/platform/uwp/`, `apps/ymir-sdl3/uwp/`, `scripts/uwp/`, `vcpkg-overlay-ports/uwp/`, `docs/uwp/`) **must be guarded**: `#if YMIR_PLATFORM_UWP` in C++, `if (Ymir_PLATFORM_UWP)` in CMake. The desktop build must keep working.
3. **Never modify the `vendor/SDL3-uwp` submodule** unless strictly necessary — it is third-party, and its `AGENTS.md` forbids AI-generated contributions to SDL. Prefer fixes in Ymir's own code.
4. **Never run `rm -rf` / `Remove-Item -Recurse` on repo directories.** To rebuild, use `cmake --fresh` (scripts accept `-Reconfigure`).
5. **Do not commit** `scripts/uwp/xbox.env`, `*.pfx`, or anything under `out/` (all gitignored).
6. One commit per fix, message format `uwp: <what changed>`.
7. Code style: 4-space indent, nested preprocessor directives indented, **comments in English**, line endings LF.
8. **Never deploy or launch the app yourself — not on the PC, not on the Xbox.** Do not run `deploy-xbox.ps1`, `Add-AppxPackage`, `Install.ps1`, `Start-Process shell:AppsFolder\...`, or attach a debugger to `ymir-sdl3.exe`. The agent's job stops at editing code and building (`pwsh scripts/uwp/build.ps1`). **Ask the user to run and test it manually in Visual Studio 2026 (F5 / deploy straight to the Xbox), and wait for their log or report before continuing.**
9. **Everything is written in English**: code comments, Doxygen blocks, commit messages, and all documentation. The conversation with the user is in Portuguese; the artifacts are not. When code must be documented or commented, always write the comment — do not leave non-obvious logic unexplained. **Deliberate exception:** `docs/uwp/HANDOFF.md` and `docs/uwp/evidence/` are in **Portuguese**, because they are the operational document the user reads every session. A file is never half in each language.
10. **Never `git commit` or `git push` unless the user explicitly asks for it** ("commit", "faz o commit", "push"). Stage or leave changes in the working tree; wait for the request.
11. **Update the shared memory before you commit.** `docs/uwp/HANDOFF.md` has a `START HERE` block at the top that is the canonical current state; rewrite it with the real state, update §1 and §5, and **append** a new entry to the session log (§0.1). Never delete or rewrite past entries. Record decisions in `docs/uwp/README.md` §4 and raw evidence in `docs/uwp/evidence/`.
12. **The other agent shares this working tree** (`F:\workspace\Ymir`). "Pulling" their work is usually a no-op — their commits already land on the local HEAD. Check `git log HEAD..origin/uwp/main` and `git status` before concluding nothing changed.

## Documentation map

| Doc | Purpose |
|---|---|
| `docs/uwp/README.md` | Port architecture, every design decision with its rationale, fork delta map |
| `docs/uwp/HANDOFF.md` | **Shared memory between the port agents**: canonical state at the top, then the sequential Xbox runbook (build → sideload → verify checklist → troubleshooting T-01..T-18) and the append-only session log |
| `docs/uwp/TESTING.md` | Testing on the Xbox without a mouse: IPL ROM and disc auto-load, hotkey bindings, gamepad-cursor feature design |
| `docs/uwp/evidence/` | Raw logs of the Xbox runs, with what each one proves and what it does not |
| `docs/uwp/` (other) | Port-specific deep dives as they are written |
| `docs/ymir/` | General developer documentation of Ymir itself: Saturn hardware primer, architecture, core, video, audio, CD/media, frontend, build, glossary |
| `CONTRIBUTING.md` | Coding, naming and architectural rules for Ymir itself, including AI usage guidelines |

## Engineering standards

- **Documentation is part of the deliverable, not an afterthought.** Any large change or large analysis gets written up under `docs/` (mirror the existing `docs/uwp/` layout: a `<topic>/README.md` for architecture and decisions, and a `<topic>/PLAN.md` or `HANDOFF.md` when there is a sequential runbook). Document **failed approaches and wrong turns too**, with the reason they failed, so they are not retried.
- **Big decisions need their rationale recorded** — what was chosen, why, and which alternative was rejected. `docs/uwp/README.md` §4 is the reference format for that.
- **SOLID and a clean architecture base by default.** This project already separates concerns strictly: `ymir-core` is the platform-agnostic backend (no frontend libraries, no vcpkg), `apps/ymir-sdl3` is an SDL3+ImGui frontend over it. Keep dependencies pointing in that direction: extend existing interfaces and utility classes rather than adding conditionals, prefer composition over inheritance and avoid `virtual` in hot paths, express errors as return values instead of exceptions, and put platform-specific code behind the existing seams (`posix`/`win32` style implementations, `YMIR_PLATFORM_UWP` guards, services in `app/services/`). Do not introduce new global/static state; Ymir deliberately puts everything in objects so multiple cores can coexist in one process.
- Naming, formatting and further style rules are in `CONTRIBUTING.md` (`.clang-format` applies to all first-party C/C++) — follow them, and run `clang-format` on modified files.

## Build and test commands

Desktop regression (run before touching shared code; uses the MSVC preset, run from an x64 Native Tools prompt):

```powershell
cmake --preset Full-x86-64-AVX2-MSVC-Release
cmake --build out\build\Full-x86-64-AVX2-MSVC-Release
ctest --test-dir out\build\Full-x86-64-AVX2-MSVC-Release --output-on-failure -j 8
```

UWP package (the normal loop for this fork):

```powershell
pwsh scripts/uwp/build-deps.ps1                    # SDL3-uwp -> out/uwp-deps (once, or when the submodule changes)
pwsh scripts/uwp/build.ps1 -Configuration Release  # -Debug for Debug; -Reconfigure to reconfigure with --fresh
pwsh scripts/uwp/package.ps1 -Configuration Release # distributable zip in out/dist
```

- `build.ps1 -Configuration Release` maps to CMake `RelWithDebInfo`. Use Release for the Xbox: Debug needs `Microsoft.VCLibs.140.00.Debug`, which is much slower.
- The `.msix` and its `Dependencies\x64\*.appx` appear under `out/build/UWP-x86-64-AVX2-MSVC/apps/ymir-sdl3/AppPackages/ymir-sdl3/ymir-sdl3_<ver>_x64_<Config>_Test/`.
- Visual Studio: `pwsh scripts/uwp/open-vs.ps1` generates and opens the solution. **The `.slnx`/`.vcxproj` under `out/build/` are generated, not committed** (they hold absolute paths). Re-run the script after any `CMakeLists.txt` change; never use *File > Open > Folder* for the UWP build.
- Deploy to Xbox (`deploy-xbox.ps1`) needs `scripts/uwp/xbox.env` copied from `xbox.env.example`.

## Expected, harmless build noise

Do not chase these: `APPX0006 ... runFullTrust`, `LNK4075 ... /INCREMENTAL`, `MASM : warning A4018: invalid command-line option : /sdl-`, and repeated `compile_hlsl_shader: This generator does not support depfiles`. Real failures are mapped in HANDOFF §4 (T-01..T-18).

## Toolchain quirks

- UWP configure **requires** `-DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0` (the preset does this); without it the vcpkg triplet `x64-uwp-static-md` fails.
- The generator is **Visual Studio 18 2026**; `scripts/uwp/common.ps1` silently falls back to the VS 2022 preset if VS 2026 is absent. Override with `-VisualStudio 2022` or `YMIR_UWP_VS=2022`.
- Never add vcpkg ports to `ymir-core`: it is deliberately vcpkg-free so the emulation core stays embeddable. Use `vendor/` submodules/source trees instead. (`libs/ymir-core` = backend, emulation only; `apps/ymir-sdl3` = SDL3+ImGui frontend. Frontend libraries must never leak into `ymir-core`.)
- Only `YMIR_PLATFORM_UWP` (0/1) gates UWP code in C++; it is a PUBLIC macro from `ymir-core`. Do not invent a second macro.

## Tests

Catch2, registered with CTest via `catch_discover_tests`. Two targets: `ymir-core-tests` (`tests/ymir-core-tests/`) for the core library, and `ymir-sdl3-tests` (`tests/ymir-sdl3-tests/`) for the frontend — it compiles the frontend sources it tests directly (e.g. `apps/ymir-sdl3/src/util/stdio_suppress.cpp`) instead of linking the app, so keep its dependency list to the library headers only. Tests are off when `ymir-core` is consumed as a subproject (`Ymir_LIBRARY_ONLY=ON`) and are also off in the UWP build, so **a desktop-only test cannot catch a UWP-only failure** — the Xbox run remains the only real coverage for those. The test count is small and changes when upstream removes targets — check `ctest` output rather than assuming a number.

## Fork / git workflow

- Branch `uwp/main`, remote `origin` = `marcelofrau/Ymir`. Tags for releases are `uwp-vX.Y.Z.R` (e.g. `uwp-v0.4.2.1`); the 4th number must increase for the Xbox to accept an update.
- `upstream` (`ymir-emu/Ymir`) is not configured in this checkout. Sync with:
  ```powershell
  git remote add upstream https://github.com/ymir-emu/Ymir.git   # if missing
  git fetch upstream; git rebase upstream/main
  git submodule update --init --recursive
  ```
  Rebase conflicts most likely in `apps/ymir-sdl3/CMakeLists.txt`, `gfx_context_impl_d3d12.cpp`, `libs/ymir-core/CMakeLists.txt`.
- CI: only `.github/workflows/uwp.yml` is active on the fork; upstream workflows are intentionally disabled (`gh workflow list -R marcelofrau/Ymir --all`). New upstream workflows arrive **enabled** after a rebase — disable them.
- PRs to upstream must comply with `CONTRIBUTING.md` "AI usage guidelines" (disclose AI assistance, write the PR/commit text yourself).

## Runtime facts that save debugging time

- Package identity: `Ymir.SaturnEmulator.UWP`, PFN `Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm`. Changing `Ymir_UWP_PUBLISHER` changes the PFN.
- The app's only log is `<data folder>\ymir-uwp.log` (stdout+stderr, line-flushed). Data folder on PC: `%LOCALAPPDATA%\Packages\Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm\LocalState\StrikerX3\Ymir\`; on Xbox: `LocalAppData\Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm\LocalState\StrikerX3\Ymir\` via Device Portal. **A 0-byte log means the crash happened before the redirect was flushed** — look at startup code, not the emulation core.
- On UWP the app instance must be set to **App type: Game** in Dev Home (App = 1 GB RAM and a GPU slice). This limit does not apply under a debugger, so debugger runs mislead.
- `SDL_GetCurrentDirectory()` and friends can return `nullptr` in an AppContainer; SDL getters that return pointers must be null-checked before wrapping in `std::string`. (`SDL_GetPrefPath` has the same hazard.)
- The development log is mirrored to the Visual Studio Output window (`Output → Debug`) through `OutputDebugStringA` while a debugger is attached, so it can be copied from there without going through Device Portal. Running from the tile (no debugger) writes to the file only.
- **The Microsoft CRT under UWP closes a stream when `freopen` fails.** After that `fileno()` returns `-1` and a later `_dup2` trips a debug assertion in `dup2.cpp`. Redirect stdio at the **descriptor** level (`dup` + `open("NUL")` + `dup2`) and never close the stream. See `util::StdioSuppressor`.
- **SDL's WASAPI audio path deadlocks on the Xbox**: `SDL_OpenAudioDeviceStream` waits on a no-timeout semaphore after `ActivateAudioInterfaceAsync`, and pumping the CoreWindow dispatcher does not unblock it. The UWP build uses its own **XAudio2** backend behind the `AudioBackend` interface; the desktop build keeps the SDL backend.
- The emulator is already driven by the gamepad (default port 1 Control Pad bindings). Only the ImGui layer needs a mouse, and the Xbox has none — see `docs/uwp/TESTING.md` for the ways to test without one.

## Xbox startup — read before touching boot, logging or threads

- **A run under the debugger is not a valid test of startup.** Debugger runs have no memory/GPU cap and different activation timing, so failures that only happen when launched from the tile will not reproduce. Validate startup changes by launching from the tile.
- The app loads, shows the splash and then **exits cleanly with no crash** if a startup step returns early — `The thread ... has exited with code 0` followed by module unloads in the VS log means shutdown, not a hang. Check the app's own log to find which step bailed.
- `The island for this CoreWindow has not been initialized.` and `WinRT: Using display resolution (gaming device unknown)` are CoreWindow/SDL3-uwp activation symptoms, not Ymir errors. `_com_error` thrown right before D3D12 loads comes from that path.

