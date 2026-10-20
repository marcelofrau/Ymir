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
| 05 | (see the quote below) | Audio-path markers prove the block is **inside** the `SDL_OpenAudioDeviceStream` call: the marker before it prints, the one after it never does. Root cause confirmed as the WASAPI activation deadlock. |

## Run 05 — the decisive measurement

```
[Ymir boot] initializing audio
[Ymir audio] Init(): opening default playback device (freq=44100 format=8010 channels=2)
'ymir-sdl3.exe' (Win32): Loaded 'C:\Windows\System32\AudioSes.dll'. ...
```

`format=8010` is `SDL_AUDIO_S16LE`; the request is 44100 Hz stereo, an entirely ordinary format. The call is entered and
**never returns**.

## Root cause (confirmed)

```
SDL_OpenAudioDeviceStream → WASAPI_OpenDevice → ActivateWasapiDevice → WASAPI_ActivateDevice
   src/audio/wasapi/SDL_wasapi_winrt.cpp:292
   → ActivateAudioInterfaceAsync(...)     :306   async; completion is delivered by the app's ASTA dispatcher
   → handler->WaitForCompletion()         :317   SDL_WaitSemaphore, no timeout
```

The completion handler is serviced by the CoreWindow dispatcher, which is pumped by the main (ASTA) thread. That thread
is the one blocked inside SDL, so the activation never completes and the semaphore is never signalled. SDL itself flags
the risk at `SDL_wasapi_winrt.cpp:316`.

Why `SDL_Init(SDL_INIT_AUDIO)` passes: it only *enumerates* endpoints. The hang is in *activation*, which happens when
the device is opened.

Contributing conditions: the main thread's COM apartment is STA (`src/core/windows/SDL_windows.c:215`,
`COINIT_APARTMENTTHREADED`); the UWP WASAPI platform init does **not** call `CoInitializeEx` itself
(`SDL_wasapi_winrt.cpp:214` returns true immediately).

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

1. **`SDL_OpenAudioDeviceStream` blocks opening the default playback device under Xbox/UWP.** **CONFIRMED by run 05.**
   See "Root cause" above. The block is the WASAPI activation deadlock, serviced by the ASTA dispatcher.
2. The block is not in SDL but in the driver/OS audio stack *after* the stream is opened — **eliminated**: the
   marker placed after the call never prints, so the call never returns.
3. State corrupted earlier (the two `_com_error` from `CoreWindow::GetForCurrentThread()` /
   `ApplicationView::GetForCurrentView()`) — **not needed as an explanation** now that the mechanism is known; the
   "island for this CoreWindow has not been initialized" symptom is a CoreWindow activation quirk, not the cause here.

## Workarounds under evaluation

1. **Open the device from a worker thread while the main thread pumps the CoreWindow dispatcher** (`SDL_PumpEvents()`),
   with a timeout. The pump path reaches
   `CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(ProcessAllIfPresent)` at
   `src/core/winrt/SDL_winrtapp_direct3d.cpp:426`, which is exactly the ASTA pump the activation needs. Implemented in
   `audio_system.cpp`; run 06 verifies it.
   - Known risk: `SDL_winrtapp_direct3d.cpp:669-672` warns that `ProcessEvents` does not return after delivering a
     *suspend* event, and the `ProcessOneAndAllPending` variant (`:435`) blocks when no events are pending. A suspend
     during the boot window can therefore turn the timeout into a hang.
   - Known risk: detaching the stuck worker on timeout leaves a thread inside SDL, which is unsafe if the app later
     calls `SDL_Quit()`.
2. **Route audio through XAudio2 instead of SDL/WASAPI** (plan B). A sibling project in the user's workspace,
   `F:\workspace\ymir-xbox-with-real-audio`, already runs real audio on the Xbox this way
   (`src/audio/XAudio2Output.cpp`): `XAudio2Create` → `CreateMasteringVoice` → `CreateSourceVoice`, fully synchronous,
   no `ActivateAudioInterfaceAsync`, no semaphore, no message-pump dependency. `xaudio2.lib` / `xaudio2.h` are present
   in SDK 10.0.28000.0 — the exact `WindowsTargetPlatformVersion` this UWP build uses — so no new external dependency
   is needed.

Local changes to `vendor/SDL3-uwp` are not an option: repo `AGENTS.md` rule 3 and the vendored SDL's own `AGENTS.md`
both forbid AI-authored contributions there.

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
