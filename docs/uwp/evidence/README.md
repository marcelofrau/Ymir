# Evidence — Xbox startup investigation (2026-10-20)

This folder holds the **raw debugger output** of every Xbox run, plus the analysis of each one. They are kept in the
repository so that any agent (or human) can reason about the failure from the same primary data, instead of relying on
a summary.

## The problem

On the Xbox (Dev Mode, launched from Visual Studio 2026), the app **stops on the splash screen and stays there**. It
does not crash, does not exit, and shows no error dialog. The process stays alive. On the PC (packaged UWP build) the
same symptom is expected but is not the current focus.

## Index of runs

| Run | File | What it established |
|---|---|---|
| 03 | `run-2026-10-20-03-texture-markers.raw.txt` | The log mirror works end to end. The D3D12 context initializes **successfully**. The boot reaches the framebuffer/display texture creation. Last marker: `[Ymir boot] display texture OK, stbi logo load next`. |
| 04 | `run-2026-10-20-04-audio-hang.raw.txt` | Adds markers down the whole path between the graphics context and the main loop. **The boot stops right after `[Ymir boot] initializing audio`, when `AudioSes.dll` (WASAPI) loads.** No marker after that, and the loop is never reached. |

## The most recent run (04) — reading

The markers are synchronous `OutputDebugStringA` calls, so they are not subject to the stdio buffering problems that
hid the log in earlier runs. The boot sequence, in order:

```
[Ymir boot] main() entered
[Ymir boot] direct file write OK (before redirect)
[Ymir boot] RedirectStdioToLogFile: fds read=4 write=5 stdout=1 stderr=2
[Ymir boot] RedirectStdioToLogFile: _dup2 OK
[Ymir boot] log redirection done: Q:\Users\UserMgr2\...\ymir-uwp.log
[Ymir boot] log pump thread started
[Ymir boot] core window bounds set
info  | VDP-Config       | Switched to Software VDP renderer
[Ymir boot] App::Run() entered
info  | App              | Ymir 0.4.2-dev
...
[Ymir boot] creating software framebuffer texture
info  | App              | Direct3D 12 graphics context initialized successfully
info  | App              | Leaving full screen mode
[Ymir boot] software framebuffer texture OK
[Ymir boot] creating display texture
[Ymir boot] display texture OK
[Ymir boot] display texture OK, stbi logo load next
[Ymir boot] initializing audio
'ymir-sdl3.exe' (Win32): Loaded 'C:\Windows\System32\AudioSes.dll'. ...
```

**There is no marker after `initializing audio`.** The next source lines in `App::Run()` are:

1. `m_context.audioSystem.Init(...)` — the marker `initializing audio` precedes it;
2. `m_context.audioSystem.Start()` — had a marker `starting audio stream` that never appears;
3. the logo texture, `SDL_ShowWindow`, ImGui init, then `while (true)` with the `[Ymir present]` traces.

Since even `starting audio stream` never prints, the block is inside **`AudioSystem::Init()`**, which calls
`SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, ...)`.

`AudioSes.dll` is the Windows Audio Session API, so the block is consistent with WASAPI enumeration/opening the default
playback endpoint stalling.

**Caveat — this is not yet proof.** `AudioSes.dll` loading only shows the WASAPI path was entered; it does not by itself
prove that `SDL_OpenAudioDeviceStream` never returns. `apps/ymir-sdl3/src/app/audio_system.cpp` was therefore instrumented
with `[Ymir audio]` markers:

- before and after `SDL_OpenAudioDeviceStream`, including its return value and `SDL_GetError()` on failure;
- in `Start()`, before and after `SDL_ResumeAudioStreamDevice`.

Run 05 with that build answers the question unambiguously.

## Hypotheses, ranked

1. **`SDL_OpenAudioDeviceStream` blocks opening the default playback device under Xbox/UWP.** Most likely, given the
   measured stopping point. Not yet confirmed — run 05 decides.
2. The block is not in SDL but in the driver/OS audio stack *after* the stream is opened (the call returns, and the app
   blocks later without a marker). Less likely, because `starting audio stream` would have printed first.
3. Something earlier silently corrupts state so that the audio call deadlocks — e.g. the two `_com_error` exceptions from
   `CoreWindow::GetForCurrentThread()` / `ApplicationView::GetForCurrentView()`. Speculative; owed to the
   "island for this CoreWindow has not been initialized" symptom that appears on every run.

## Root cause analysis of run 04 (source reading, 2026-10-20)

Reading `vendor/SDL3-uwp/src/audio/wasapi/` explains the hang. Opening the default playback device on UWP goes
through two blocking waits on two threads:

1. `SDL_OpenAudioDeviceStream()` on the **UI thread** (the thread that runs `main()`) reaches `WASAPI_OpenDevice()` →
   `ActivateWasapiDevice()`, which proxies the work to SDL's WASAPI **management thread** with
   `WASAPI_ProxyToManagementThread(..., wait_on_result)` and **blocks the UI thread** until it returns
   (`SDL_wasapi.c`, `mgmtthrtask_ActivateDevice`).
2. On the management thread, `WASAPI_ActivateDevice()` (`SDL_wasapi_winrt.cpp`) calls `ActivateAudioInterfaceAsync()`
   and **blocks on a semaphore** until the completion handler fires. SDL itself flags this wait with a `FIXME`
   ("this can block indefinitely...").

`ActivateAudioInterfaceAsync()` is documented to be called from the UI thread in UWP apps. When it is called from
another thread, the activation is serviced through the app's UI (ASTA) thread. That thread is blocked in step 1, so
the completion handler never runs: **a deadlock**. This matches every observation in run 04:
- the process stays alive;
- `AudioSes.dll` is the last module loaded;
- no marker prints after `initializing audio`, including `SDL_OpenAudioDeviceStream returned`.

### Fix (pending run 05 to confirm)

`apps/ymir-sdl3/src/app/audio_system.cpp` → `OpenAudioDeviceStreamWithoutBlockingUIThread()` (UWP only):
- opens the device from a worker thread;
- meanwhile the UI thread keeps calling `SDL_PumpEvents()`, which runs `CoreDispatcher::ProcessEvents` on WinRT, so the
  activation can complete;
- if the device is still not open after 10 s, `Init()` fails with `Timed out after 10 seconds opening the default
  audio playback device`, so the app reports a clear startup failure instead of hanging.

`SDL_PumpEvents()` is safe to call while the worker is inside `SDL_OpenAudioDeviceStream()`: the only audio work it
does is `SDL_UpdateAudio()`, which takes `subsystem_rwlock` briefly and never waits on the device lock held by the
open path.

The SDL3-uwp submodule is left untouched on purpose (see `AGENTS.md`, rule 3).

**What run 05 must show:** `[Ymir audio] Init(): SDL_OpenAudioDeviceStream returned a stream`, then
`Start(): OK`, then `Audio stream opened: 48000 Hz, ...` and the main loop. If it shows the timeout message instead,
the deadlock hypothesis is wrong and the activation never completes for another reason. The next step would then be
to look at the WinRT device watcher / `GetDefaultAudioRenderId` path, or at the audio capabilities in the manifest.

## Hypotheses already eliminated

| Hypothesis | Why it is dead |
|---|---|
| `swapchain->Present(1, 0)` blocks on a vblank that never comes | **The loop is never reached.** Run 04 stops well before the first frame. The `[Ymir present]` traces added for it produced no output at all. |
| The app returns early / exits cleanly (entry #2 of the handoff) | The process stays alive; there is no `ShowStartupFailure` and no `goto end_loop`. |
| The D3D12 context or the swapchain fails to create (the old T-12 recipe) | `Direct3D 12 graphics context initialized successfully` is logged on every run. |
| `GetGamingDeviceModelInformation()` failing (`gaming device unknown`) is the cause | It does fail, but it makes the code fall back to 1920x1080, which matches the console's actual configured output mode. Cosmetic only. |

## Environment facts for reproducing

- Launched with **Visual Studio 2026, F5 straight to the Xbox**, from the machine where the port was built.
- Package: `Ymir.SaturnEmulator.UWP`, PFN `Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm`.
- App log: `<data folder>\ymir-uwp.log`; on Xbox,
  `LocalAppData\Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm\LocalState\StrikerX3\Ymir\ymir-uwp.log` via Device Portal.
- Startup failure that happens **only** when launched from the tile (no debugger) cannot be reproduced here; per
  `AGENTS.md`, validate startup by launching from the tile.
- Build for these runs: `pwsh scripts/uwp/build.ps1 -Configuration Debug` (Debug/RelWithDebInfo).
- **Close Visual Studio before rebuilding**: `devenv` holds `ymir-sdl3.pdb` and the link fails with `LNK1201`.
