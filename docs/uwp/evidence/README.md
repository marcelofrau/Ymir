# Evidence — Xbox startup investigation (2026-10-20)

This folder holds the **raw debugger output** of every Xbox run, plus the analysis of each one. They are kept in the
repository so that any agent (or human) can reason about the failure from the same primary data, instead of relying on
a summary.

## The problem

On the Xbox (Dev Mode, launched from Visual Studio 2026), the app **stopped on the splash screen and stayed there**
(runs 03–06, the audio deadlock). It did not crash, did not exit, and showed no error dialog. The process stayed
alive. On the PC (packaged UWP build) the same symptom is expected but is not the current focus.

**Current symptom (run 09):** the app **runs on the console** — the CRT assertion is gone and the Ymir GUI appears.
What is missing is not the boot but the **input**: the GUI is mouse-and-keyboard driven and the Xbox has no mouse.
See "Run 09" below and `docs/uwp/TESTING.md`.

## Index of runs

| Run | File | What it established |
|---|---|---|
| 03 | `run-2026-10-20-03-texture-markers.raw.txt` | The log mirror works end to end. The D3D12 context initializes **successfully**. The boot reaches the framebuffer/display texture creation. Last marker: `[Ymir boot] display texture OK, stbi logo load next`. |
| 04 | `run-2026-10-20-04-audio-hang.raw.txt` | Adds markers down the whole path between the graphics context and the main loop. **The boot stops right after `[Ymir boot] initializing audio`, when `AudioSes.dll` (WASAPI) loads.** No marker after that, and the loop is never reached. |
| 05 | (see the quote below) | Audio-path markers prove the block is **inside** the `SDL_OpenAudioDeviceStream` call: the marker before it prints, the one after it never does. Root cause confirmed as the WASAPI activation deadlock. |
| 06 | `run-2026-10-20-06-worker-thread-pump-refuted.raw.txt` | The worker-thread + dispatcher-pump workaround. **Refuted:** ~5000 `SDL_PumpEvents()` calls over 10 seconds do not unblock the activation. The timeout produces a clean failure, and the detached worker outlives the VDP render threads (the orphan-thread risk). |
| 07 | (no log kept) | First run with the XAudio2 backend, F5 with a **Debug** build. **The splash is dismissed and the screen turns black**, and stays that way. No output was saved. Reading below. |
| 08 | `run-2026-10-21-08-midi-stdio-assert.raw.txt` | The XAudio2 backend works and the boot **completes**: D3D12, ImGui, audio, emulator thread and the main loop all start, with a correct window geometry (1920x1080, not minimized, focused, `io.DisplaySize` 1920x1080). The process then **hits a debug assertion** in `util::StdioSuppressor::~StdioSuppressor()` during MIDI init. |
| 09 | (no log kept) | The CRT assertion is gone and **the Ymir GUI appears** on the console. The port is usable. No output was saved. The open problem is now the input, not the boot: the GUI is mouse-driven. |

## Run 09 — the app runs, the GUI appears (2026-10-21)

Raw log: not captured (user report only).

### What it proves

- The `StdioSuppressor` fix works: the process no longer trips the CRT assertion in the MIDI init.
- The whole boot path is stable on the console: D3D12, ImGui, XAudio2, emulator thread, main loop, frames presented,
  window 1920x1080 with the correct geometry.
- **The Ymir GUI renders on the Xbox.**

### What it does not prove

- Nothing about the emulation itself: there is no IPL ROM and no disc on the console yet.
- Nothing about the input: the GUI is mouse-and-keyboard driven, and the console has no mouse. The emulator is
  already gamepad-driven (default port 1 bindings), so the only gap is navigating the ImGui layer.

### Next run

See `docs/uwp/TESTING.md` for the file layout that makes the boot GUI-free (IPL ROM in `roms/ipl/`, disc in
`state/recent_discs.txt`, `RememberLastLoadedDisc = true`).

## Run 08 — the boot completes, then a CRT assertion in the MIDI init (2026-10-21)

Raw log: `run-2026-10-21-08-midi-stdio-assert.raw.txt`.

### What it proves

```
[Ymir gfx] D3D12 enhanced barriers reported: not supported; forced off on UWP
[Ymir boot] ImGui backends initialized
info  | GfxContext       | Dear ImGui backends initialized: yes
[Ymir audio] XAudio2 engine created
[Ymir audio] XAudio2 mastering voice created (2 ch, 44100 Hz)
[Ymir audio] XAudio2 source voice created
[Ymir audio] XAudio2 playback started
[Ymir boot] saturn reset OK
[Ymir boot] emulator thread started
[Ymir boot] window shown
[Ymir boot] entering main loop
[Ymir boot] frame 0: window 1920x1080 (1920x1080 pixels), flags 632 (hidden=false minimized=false occluded=false input focus=true), ImGui display 1920x1080, framebuffer scale 1x1
```

- **The XAudio2 backend works on the Xbox.** `XAudio2Create`, `CreateMasteringVoice`, `CreateSourceVoice` and `Start`
  all return. The WASAPI deadlock is resolved.
- **The whole boot path passes.** Window geometry is correct: 1920x1080, not hidden, not minimized, not occluded, has
  input focus, `io.DisplaySize` is 1920x1080, framebuffer scale 1x1.
- **The CoreWindow was activated** — the splash was dismissed, and that only happens in `CoreWindow::Activate()`,
  which SDL3-uwp calls from the first `SDL_PollEvent()` of the main loop.
- The debug layer no longer breaks on warnings, and the ImGui DX12 backend initialized without asserting.

### The failure

```
Debug Assertion Failed!
File: minkernel\crts\ucrt\src\appcrt\lowio\dup2.cpp
Line: 67
Expression: ((unsigned)target_fh < _NHANDLE_)

ymir-sdl3.exe!util::StdioSuppressor::~StdioSuppressor() Line 40
ymir-sdl3.exe!app::services::MIDIService::DoInit(...) Line 178
```

`apps/ymir-sdl3/src/util/stdio_suppress.cpp` used `freopen` to point the stream at the null device:

```cpp
m_fd_prev = dup(fileno(file));
freopen_s(&file, kNullFile, "w", file);   // closes the stream when it fails
...
dup2(m_fd_prev, fileno(m_file));           // fileno() == -1 -> assert
```

`freopen` **closes the stream when it fails**. Under UWP the reopen fails, `fileno(stderr)` becomes `-1`, and the
restoring `_dup2(m_fd_prev, -1)` trips the CRT assertion. As a side effect `freopen` also closed the descriptor of
the log pipe, killing the thread that drains the log.

### Fix applied

The redirection now works at the **file descriptor** level and never closes the stream:

- `dup` the current descriptor into `m_fd_prev`;
- `open` the null device into `m_fd_null`;
- `dup2(m_fd_null, m_fd_target)`;
- on destruction, `dup2(m_fd_prev, m_fd_target)` and close both temporaries.

The target descriptor is cached in `m_fd_target` and the restore uses the cached value instead of calling `fileno()`
again, so a stream that went bad can no longer produce an invalid descriptor. If the null device cannot be opened at
all, the suppression is skipped and a `warn` is logged instead of crashing.

**Side benefit:** the log pipe's descriptor is never closed by the suppressor, so the log pump thread survives it.

### Still open

- Whether the null device is reachable at all in an AppContainer. If it is not, the RtMidi noise stays in the log
  (harmless) and the line `warn | StdioSuppress | Cannot open 'NUL' ...` will say so.
- Whether frames are actually presented. Run 08 died before the first present trace could be read.

## Run 07 — the splash goes away, then a black screen (2026-10-21)

No log was kept from this run; the only data is the user's description and the fact that it was a Debug build
launched with F5. The symptom still carries a lot of information.

### What "the splash went away" proves

- The splash is dismissed by `CoreWindow::Activate()`, which SDL3-uwp only calls from `SDL_WinRTApp::OnAppActivated`
  (`vendor/SDL3-uwp/src/core/winrt/SDL_winrtapp_direct3d.cpp:613-615`).
- That handler is dispatched by the CoreWindow dispatcher, which is only pumped by `SDL_PumpEvents()`
  (`:426`). `SDL_ShowWindow()` does not pump (the WinRT driver has no ShowWindow hook), and neither do the D3D12
  init, the XAudio2 backend nor any other step of the boot.
- So the first pump is the first `SDL_PollEvent()` of the main loop (`apps/ymir-sdl3/src/app/app.cpp`, "Process SDL
  events"). **Run 07 got past the audio initialization — XAudio2 works — and reached the main loop.** This also
  explains why the splash stayed up during the audio deadlock of runs 04–06: the window was never activated.

### What the black screen means

Without a BIOS, the Saturn output is black, so the only visible things should be the ImGui menu bar and the
*Welcome* modal (`app.cpp`, `OpenWelcomeModal(true)`). A black screen means either the UI thread stopped in the
first frames, or frames are presented without any UI in them.

### Hypotheses, ranked

1. **Visual Studio is in break mode (Debug build).** Two first-frame-only sources of a break:
   - The D3D12 debug layer is set to break on **warnings**, not just errors
     (`gfx_context_impl_d3d12.cpp` calls `DebugLayer::BreakOnWarnings(device, true)`, which sets
     `SetBreakOnSeverity` for WARNING, ERROR and CORRUPTION, `libs/ymir-core/src/ymir/gpu/d3d12/d3d12_debug.cpp`).
     The first `ClearRenderTargetView`/`Present` is the first frame the runtime validates.
   - `ImGui_ImplDX12_NewFrame` creates the ImGui root signature and pipeline lazily on the first frame and asserts if
     that fails (`vendor/imgui/imgui/backends/imgui_impl_dx12.cpp`, `IM_ASSERT(0 && "...CreateDeviceObjects()
     failed!")`). That includes compiling the ImGui shaders **at runtime** with `D3DCompile` (vs_5_0/ps_5_0, DXBC).
   Under the debugger either one pauses the process, which looks exactly like "alive, black, never moves".
2. **`Present(1, 0)` or the frame fence wait blocks on the CoreWindow.** This was "eliminated" after run 04 only
   because the loop was never reached; it was **never actually tested** and is open again.
3. **Frames are presented but ImGui draws nothing.** The Release-build face of hypothesis 1 (no assert, silent
   failure), or `io.DisplaySize` being zero because SDL flagged the window as minimized
   (`imgui_impl_sdl3.cpp`, `ImGui_ImplSDL3_GetWindowSizeAndFramebufferScale`). The latter only happens after a
   `VisibilityChanged(false)` (`SDL_winrtapp_direct3d.cpp:528-530`), which is unlikely at startup.
4. **The UI thread blocks inside `SDL_PollEvent()`.** If SDL3-uwp thinks the window is invisible and no
   hidden/minimized event is pending, `PumpEvents` switches to `ProcessEvents(ProcessOneAndAllPending)`, which waits
   for an event (`SDL_winrtapp_direct3d.cpp:390-436`).

Ruled out by reading the code: the video-sync wait on `frameReadyEvent` (`syncInWindowedMode` defaults to `false`
and the log says `Leaving full screen mode`), and audio sync (it can only stall the emulator thread, not the UI).

### Diagnostic gaps found while analysing it

- `YMIR_BOOT_TRACE`/`YMIR_PRESENT_TRACE` only called `OutputDebugStringA`, which goes nowhere without a debugger, so a
  run launched from the tile left no trace in `ymir-uwp.log`.
- `YMIR_PRESENT_TRACE` wrote 3+ lines per frame, flooding the Output window at 60 fps.
- The XAudio2 backend logged with `SDL_LogInfo(SDL_LOG_CATEGORY_AUDIO, ...)`, but SDL filters every category except
  APPLICATION at ERROR priority by default (`vendor/SDL3-uwp/src/SDL_log.c:397-410`), so those lines were never
  printed.
- The return value of `ImGuiInit()` was ignored.

### Instrumentation for run 08 (all under `YMIR_PLATFORM_UWP`)

| Change | Where |
|---|---|
| `platform::uwp::Trace()`: synchronous write to the debugger **and** `ymir-uwp.log`; all temporary traces use it | `platform/uwp/uwp_platform.{hpp,cpp}`, `app.cpp`, `main.cpp`, `xaudio2_backend.cpp` |
| Present trace in full for the first 10 frames, then one frame every 5 s, with the frame number | `gfx_context_impl_d3d12.cpp` |
| D3D12 debug layer no longer breaks on messages (it still prints them to the Output window) | `gfx_context_impl_d3d12.cpp` |
| ImGui DX12 device objects created right after `ImGui_ImplDX12_Init`; a failure is logged and `App::RunEmulator` stops with a startup failure instead of a first-frame assert or a silent black screen | `gfx_context_impl_d3d12.cpp`, `app.cpp` |
| `StallWatchdog`: the main loop marks each phase; a monitor thread logs `[Ymir watchdog] UI thread stalled for N ms in phase '...' (frame N)` after 3 s without progress | `platform/uwp/stall_watchdog.{hpp,cpp}`, `app.cpp` |
| Window/ImGui geometry (size in DIPs and pixels, flags, `io.DisplaySize`, framebuffer scale) on the first frame and on every change | `app.cpp` |

### Cross-check against other Xbox ports (2026-10-21, source reading only)

Read in the user's workspace (`D:\workspace\_non_work_\`). The user's own ports: scummvm-uwp, dosbox-pure-uwp,
sm64ex-uwp, xbox-uwp-lab. Others already in the workspace: XBSX2, RetroArch, SDL3-uwp, xenia-canary-uwp (UWP code
only on `origin/Blades-UWP`), dolphin, ppsspp. Shallow-cloned for this purpose from the Emulation Revival list:
xemu-uwp and Cemu-UWP (`uwp-d3d12` branches, DanP142), Azahar-UWP, flycast.

| Hypothesis | What the working ports show |
|---|---|
| H1, debugger break | **Best supported.** No port breaks on D3D12 *warnings* by default. xenia, Dolphin and XBSX2 only break when validation is opted into, and XBSX2 additionally requires `IsDebuggerPresent()`; RetroArch and SDL3-uwp's D3D12 renderer break on ERROR only; xemu and Cemu only log. Most deny-list `CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE`, and XBSX2 also denies `INCOMPATIBLE_BARRIER_LAYOUT` because of a validation bug in the OS D3D12 (616). scummvm hit a real "debug layer breakpoint on Xbox" (commit `226d29f`) |
| H2, `Present(1, 0)` blocks | **Weakened.** `Present(1, 0)` on a CoreWindow swap chain works on Xbox in RetroArch (UI thread), XBSX2, xemu, Cemu and SDL3-uwp, the latter also with the same present-then-fence-wait pattern |
| H3, ImGui draws nothing | **`D3DCompile` itself is ruled out:** RetroArch (exactly vs_5_0/ps_5_0), Dolphin, XBSX2, flycast and xemu compile shaders at runtime on Xbox with the OS `d3dcompiler_47.dll`. A pipeline or init failure remains possible; xemu checks `ImGui_ImplDX12_Init`/`CreateDeviceObjects`, as Ymir now does |
| H4, event pump blocks | Mechanism confirmed in SDL3-uwp, but not observed in any port |
| H5, activation deadline | Not applicable under F5 (the debugger disables it, and it kills rather than freezes). Relevant for tile launches: scummvm's `docs/XBOX-TILE-LAUNCH-KILL.md` |

**Differences between Ymir and every working port**, all aligned on UWP in the working tree (README §4, D17–D19):

1. **Enhanced barriers.** XBSX2 forces them off on Xbox ("Not supported on Xbox"). Ymir used them whenever
   `EnhancedBarriersSupported` was true, on every frame (back buffer transitions) and in the VDP HW renderer. Now
   forced off on UWP; the reported value is traced (`[Ymir gfx] D3D12 enhanced barriers reported: ...`).
2. **Waitable swap chain never waited on**, with `SetMaximumFrameLatency(3)`. SDL3-uwp dropped both on WinRT, RetroArch
   defaults them off on WinRT, the others never use them or wait with a timeout. Now off on UWP.
3. **Tearing.** xenia and SDL3-uwp turn it off on UWP. Now off for CoreWindow swap chains.
4. **`DXGI_SCALING_NONE`.** xenia, Dolphin, XBSX2 and Cemu use STRETCH (xenia to fix a zoom bug). Now STRETCH on UWP.
5. **Break on WARNING.** Now off on UWP (instrumentation section above).

Not changed, noted for later: XBSX2 and xemu clear and present the back buffers right after creating the swap chain;
XBSX2 caps the swap chain to 1440p on Series S ("if you try to make a 4K Swap Chain the application will crash"),
which SDL3-uwp's `SDL_GetWindowSizeInPixels` already does.

Because all five changes go into the same build, a working run 08 will not say which one fixed it. The traces keep
what the console reports (enhanced barriers) and the debug layer still prints its messages, so bisecting later is
possible by reverting them one at a time.

### How to read run 10

Run 09 is explained above. Run 10 is the first run with the emulation actually loaded (IPL ROM and disc in place).

| Output shows | Conclusion |
|---|---|
| `Debug Assertion Failed!` in `dup2.cpp` again, but from a different frame | A second `_dup2` with an invalid descriptor exists; send the stack |
| `Loading IPL ROM from ...` followed by `IPL ROM loaded successfully` | The BIOS was found and loaded: the *Welcome* modal is gone |
| `Loading disc image ...` | The disc was auto-loaded from `state/recent_discs.txt` |
| Neither line | The files are not in the profile folder, or `RememberLastLoadedDisc` is still `false` |
| `warn \| StdioSuppress \| Cannot open 'NUL' to suppress output: errno N` | The null device is not reachable in the AppContainer. The suppression is skipped, the app keeps running, and the RtMidi noise stays in the log. Not fatal, but it needs another mechanism |
| No `warn` from `StdioSuppress` | The null device opened fine and the suppression works |
| `[Ymir present] ...` lines every 5 s | Frames are being presented; the UI should be visible |
| `[Ymir watchdog] ... stalled ... in phase '...'` | The phase name says exactly where the UI thread stopped |
| No `[Ymir present]` line at all, and no stall | The main loop is not reaching the present; look at the last phase marker |
| `D3D12 WARNING`/`D3D12 ERROR` lines | The debug layer prints them without breaking now; they are pure information |

## Run 06 — the pump workaround is refuted

```
[Ymir audio] open-on-worker: starting worker thread
[Ymir audio] open-on-worker: worker calling SDL_OpenAudioDeviceStream
'ymir-sdl3.exe' (Win32): Loaded 'C:\Windows\System32\AudioSes.dll'. ...
[Ymir audio] open-on-worker: wait loop ended after 4994 pump iterations, done=false
[Ymir audio] open-on-worker: TIMED OUT, detaching the worker thread
```

The pump chain was traced in the vendored sources and does reach
`CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent)` at
`src/core/winrt/SDL_winrtapp_direct3d.cpp:426` — so the mechanism exists and is reached — but pumping it does not
complete the audio endpoint activation. **The assumption that the CoreWindow dispatcher services that callback is
false.**

Side effects visible in the same run: the timeout turns the hang into a clean
`[Ymir] Startup failure: Failed to create audio stream: ...` and the message reaches the VS Output window (the log
tee works end to end); the `VDP1`/`VDP2`/`deinterlace render thread` threads exit while the **detached** audio worker
is still alive inside SDL, which is the orphan-thread risk recorded in the entry above.

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

1. **Open the device from a worker thread while the main thread pumps the CoreWindow dispatcher** — **REFUTED by
   run 06.** ~5000 pumps in 10 seconds did not unblock the activation. The implementation was removed.
2. **Route audio through XAudio2 instead of SDL/WASAPI** — **IMPLEMENTED.** XAudio2 is the native Xbox audio API and
   is fully synchronous: `XAudio2Create` → `CreateMasteringVoice` → `CreateSourceVoice`, none of which involve
   `ActivateAudioInterfaceAsync`, a semaphore or a message pump, so the observed failure mode does not exist.

   Feasibility was verified rather than assumed: `xaudio2.h` / `xaudio2.lib` ship in SDK 10.0.28000.0 — the
   `WindowsTargetPlatformVersion` this build targets — and `xaudio2.lib` imports directly from `XAudio2_9.dll`
   (present in `System32`), with no apiset layer. The UWP build compiles and links it; the MSIX is produced.

   Layout: `audio_backend.hpp` (pull-based interface, owns `Sample`), `sdl_audio_backend.{hpp,cpp}` (desktop),
   `xaudio2_backend.{hpp,cpp}` (UWP/Xbox), and `audio_system.cpp` picks between them behind a single `#if`.

   SDK pitfall found on the way: in this SDK version `IXAudio2::CreateMasteringVoice` takes **7** parameters
   (`ppMasteringVoice, InputChannels, InputSampleRate, Flags, DeviceId, pEffectChain, StreamCategory`); there is no
   `ChannelMask` parameter, which older examples show.

A sibling project in the user's workspace, `F:\workspace\ymir-xbox-with-real-audio`, already runs real audio on the
Xbox through this same API (`src/audio/XAudio2Output.cpp`), which is what pointed at this route.

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
| ~~`swapchain->Present(1, 0)` blocks on a vblank that never comes~~ **REOPENED by run 07** | It was dismissed because run 04 never reached the loop, not because it was tested. Since run 07 reaches the loop, it is a live hypothesis again (see "Run 07"). |
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
