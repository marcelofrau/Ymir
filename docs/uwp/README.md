# Ymir para UWP / Xbox Dev Mode — Arquitetura e decisões

> Documento de referência do port. O passo a passo de execução fica em [HANDOFF.md](HANDOFF.md).
> Última atualização: 2026-10-20 (baseado no upstream Ymir 0.4.2, `8cdf0a53`).

## 1. Objetivo

Rodar o frontend `ymir-sdl3` do Ymir (emulador de Sega Saturn) como app **UWP** num **Xbox Series X|S em Dev Mode**,
usando o **renderer VDP por hardware em Direct3D 12**. O mesmo pacote também roda no Windows 10/11 como app UWP, o
que facilita testar antes de ir pro console.

## 2. Estratégia de repositório

- **Fork:** `marcelofrau/Ymir`, branch **`uwp/main`**. O upstream é `ymir-emu/Ymir` (remote `upstream`).
- **Fork do mio:** `marcelofrau/mio`, branch **`uwp`** (submódulo `vendor/mio`). O mio é a biblioteca de *memory-mapped
  files* que o Ymir usa pra ler imagens de disco e o arquivo de backup RAM. No Windows ela chama `CreateFileW`,
  `CreateFileMapping` e `MapViewOfFile`, que não existem para apps UWP. O patch (um commit, cerca de 40 linhas) usa as
  variantes `*FromApp` quando compila na partição APP; o desktop continua igual. É candidato a PR no `StrikerX3/mio`.
- **Atualização:** `git fetch upstream && git rebase upstream/main` periodicamente. O delta é pequeno e
  concentrado; veja a seção 6.
- **Por que fork e não wrapper com submódulo:** a mudança toca core, frontend e CMake ao mesmo tempo, e o upstream
  está mexendo ativamente justo nas áreas afetadas (vdp-hw, d3d12). Patches aplicados por um wrapper quebrariam a
  cada atualização. Esse é o mesmo padrão de XBSX2, DolphinUWP e Starfox64Recomp-uwp.
- **Regras do delta:**
  1. Código exclusivo do UWP fica em diretórios próprios (`apps/ymir-sdl3/src/platform/uwp/`, `apps/ymir-sdl3/uwp/`,
     `scripts/uwp/`, `vcpkg-overlay-ports/uwp/`, `docs/uwp/`).
  2. No código compartilhado, use **uma macro só**: `YMIR_PLATFORM_UWP` (0/1).
  3. Correções genéricas, que também servem pro desktop, são candidatas a PR no upstream (lista na seção 7).

## 3. Visão geral da arquitetura

```
┌─────────────────────────────── Pacote .msix (Ymir.SaturnEmulator.UWP) ───────────────────────────────┐
│                                                                                                      │
│  ymir-sdl3.exe  (CMAKE_SYSTEM_NAME=WindowsStore, C++20, sem /ZW)                                     │
│   ├─ main.cpp ── #include <SDL3/SDL_main.h> ──► WinMain do SDL3-uwp ──► CoreApplication::Run         │
│   │                                             └─► chama main() (renomeado para SDL_main) na UI thread│
│   ├─ platform/uwp/uwp_platform.cpp  (C++/WinRT)                                                      │
│   │     ├─ RedirectStdioToLogFile()  → LocalState\StrikerX3\Ymir\ymir-uwp.log                        │
│   │     ├─ FindExternalProfilePath() → <D..Z>:\Ymir gravável, senão LocalState                       │
│   │     └─ ShowFileDialog()          → FileOpenPicker / FileSavePicker / FolderPicker                │
│   ├─ GfxContext D3D12 ── CreateSwapChainForCoreWindow (SDL_PROP_WINDOW_WINRT_WINDOW_POINTER)         │
│   └─ ymir-core (lib estática)                                                                        │
│         ├─ CPUs interpretadas (sem JIT → sem codeGeneration)                                         │
│         ├─ VDP renderer HW D3D12 (shaders DXIL pré-compilados e assinados no build)                  │
│         ├─ VirtualMemory → CreateFileMappingFromApp / MapViewOfFileFromApp                           │
│         └─ mio (imagens de disco, backup RAM) → CreateFile2FromAppW / *FromApp                       │
│                                                                                                      │
│  SDL3.dll  (SDL3-uwp 3.4.16, backend WinRT: CoreWindow, WASAPI, Windows.Gaming.Input)                │
│  Assets\*.png  (tiles e splash)                                                                      │
└──────────────────────────────────────────────────────────────────────────────────────────────────────┘
Dependência de framework: Microsoft.VCLibs.140.00 (Release) / Microsoft.VCLibs.140.00.Debug (Debug)
```

### 3.1 Pipeline de build

```
scripts/uwp/build-deps.ps1
  └─ cmake (VS 2026, WindowsStore) vendor/SDL3-uwp → out/uwp-deps/{bin/SDL3.dll, lib/SDL3.lib, cmake/SDL3Config.cmake}

scripts/uwp/build.ps1  (ou: cmake --preset UWP-x86-64-AVX2-MSVC && cmake --build --preset UWP-x86-64-AVX2-MSVC-Release)
  ├─ vcpkg manifest (triplet x64-uwp-static-md + overlays vcpkg-overlay-ports/uwp)
  │     cereal, cxxopts, date, miniz, neargye-semver, nlohmann-json, rtmidi (dummy), tomlplusplus (header-only), zlib, zstd
  ├─ DXC do Windows SDK compila HLSL → DXIL assinado (.cso) → embutido via CMakeRC
  ├─ ymir-core.lib, imgui, lz4, chdr, fmt, ...
  └─ ymir-sdl3.vcxproj (app UWP) → MSBuild AppxPackage → AppPackages\ymir-sdl3\ymir-sdl3_<ver>_x64_<Config>_Test\
        ├─ ymir-sdl3_<ver>_x64_<Config>.msix   (assinado com CN=CMake Test Cert)
        ├─ ymir-sdl3_<ver>_x64_<Config>.cer
        ├─ Dependencies\x64\Microsoft.VCLibs.x64[.Debug].14.00.appx
        └─ Add-AppDevPackage.ps1 / Install.ps1  (instalação no PC)
```

### 3.2 CI/CD (GitHub Actions)

O workflow `.github/workflows/uwp.yml` roda no runner `windows-2025`:

| Gatilho | O que faz |
|---|---|
| push ou PR em `uwp/**` | build Release; zip como *artifact* do workflow |
| tag `uwp-vX.Y.Z.R` (ex.: `uwp-v0.4.2.1`) | build com a versão da tag; **GitHub Release** com o zip e os símbolos |
| manual (`workflow_dispatch`) | build; com `release: true`, publica uma *prerelease* `uwp-v<versão>` |

- **Versão do pacote:** vem da tag, ou de `<versão do Ymir>.<run_number>` nos demais casos.
- **Prefixo `uwp-`:** separa estas tags das tags `v*` do upstream.
- **Cache:** guarda os binários do vcpkg (`.vcpkg-cache`) e o `out/uwp-deps` (SDL3-uwp).
- **Toolchain:** VS 2026 se o runner tiver (e o CMake suportar), senão VS 2022 com o preset `UWP-x86-64-AVX2-MSVC-VS2022`.
  Quem escolhe é `scripts/uwp/common.ps1`.
- **Workflows do upstream** (desktop Windows/Linux/macOS/FreeBSD, nightly, releases, screenshotter): os arquivos
  **não foram alterados**, para não gerar conflito em rebases, mas estão **desligados no fork** desde 2026-10-20 com
  `gh workflow disable`. O foco deste fork é só UWP / Xbox Dev Mode; o build desktop fica a cargo do upstream. O único
  workflow ativo é `UWP (Xbox)`. Para conferir: `gh workflow list -R marcelofrau/Ymir --all`. Para religar um:
  `gh workflow enable <id> -R marcelofrau/Ymir`. Workflows novos que o upstream adicionar vêm **ativos** depois de um
  rebase; desligue-os do mesmo jeito.

O conteúdo do zip distribuível (`scripts/uwp/package.ps1`) segue o mesmo formato de dosbox-pure-uwp, scummvm-uwp e
x-files-uwp:

```
ymir-uwp_<ver>_x64.zip
├─ ymir-sdl3_<ver>_x64_RelWithDebInfo.msix          → pacote do app (assinado)
├─ ymir-sdl3_<ver>_x64_RelWithDebInfo.cer           → certificado (o PC precisa confiar nele)
├─ Dependencies\x64\Microsoft.VCLibs.x64.14.00.appx → dependência de framework (instalar junto no Xbox)
├─ Install.ps1, Add-AppDevPackage.ps1, Add-AppDevPackage.resources\  → instalador pro PC (gerado pelo VS)
└─ INSTALL.md                                       → instruções pra Xbox e PC
ymir-uwp_<ver>_x64_symbols.zip                      → .appxsym (símbolos pra analisar crash dumps)
```

### 3.3 Fluxo de runtime no UWP

1. O CoreApplication inicia, o SDL3-uwp cria o `IFrameworkView` e chama `SDL_main` na UI thread (ASTA).
2. `main()` cria um `argv` falso (`"Ymir"`), já que o WinRT não passa argumentos, e redireciona stdout/stderr pro
   arquivo de log.
3. `App::Run`:
   - perfil em `X:\Ymir` (se existir e for gravável) ou em `LocalState\StrikerX3\Ymir\`;
   - não aparece o diálogo de primeira execução.
4. O contexto gráfico D3D12 cria device FL 11_0 e swap chain no CoreWindow, com o tamanho vindo de
   `SDL_GetWindowSizeInPixels`. O patch do SDL3-uwp reporta a resolução real da TV.
5. Com **Settings > Video > Use hardware acceleration** ligado, o VDP usa `UseDirect3D12Renderer`. Se falhar, volta
   automaticamente pro renderer por software (`events/emu_event_factory.cpp`).
6. Diálogos de arquivo usam pickers WinRT (corrotina `fire_and_forget`, conclusão entregue pelo `ProcessEvents` do SDL).
7. O áudio é aberto pelo backend **XAudio2** (`xaudio2_backend.cpp`): `XAudio2Create` → `CreateMasteringVoice` →
   `CreateSourceVoice` → `Start`. O WASAPI do SDL3 trava no console (ver D20).
8. stdout/stderr são redirecionados para um pipe, e uma threadPump escreve cada linha no arquivo de log **e** em
   `OutputDebugStringA` (quando há debugger anexado, o Output do VS espelha o devlog). O redirecionamento é feito por
   descritor, nunca com `freopen` (ver D21).

## 4. Decisões técnicas (com motivo)

| # | Decisão | Motivo | Alternativa descartada |
|---|---|---|---|
| D1 | Fork `marcelofrau/Ymir`, branch `uwp/main` | O delta toca core, frontend e build | Wrapper + submódulo + patches |
| D2 | Gerador **Visual Studio 18 2026** no preset UWP | O CMake gera um projeto UWP real (AppContainer, manifest, `.msix`, `resources.pri`, assinatura, dependência VCLibs) sem script de empacotamento manual | Ninja + makeappx/makepri/signtool manuais |
| D3 | SDL3-uwp **pré-compilado como DLL** em `out/uwp-deps` (`build-deps.ps1`) | Caminho da DLL conhecido no configure, então `VS_DEPLOYMENT_CONTENT` funciona. O SDL3-uwp compila C++/CX (`/ZW`) internamente, isolado na DLL | `add_subdirectory` (DLL em caminho por configuração, difícil de empacotar); SDL estático (mistura `/ZW` no exe) |
| D4 | O WinMain vem do `SDL_main.h` do SDL3-uwp; nada de `/ZW` no Ymir | O `SDL_main_impl.h` aceita C++20 sem `/ZW` (`_MSVC_LANG >= 202002L`) | App.cpp próprio com `IFrameworkView` |
| D5 | **Sem DXC em runtime no UWP** (`gpu_shaders_uwp.cpp`): validação vira no-op e compilação em runtime retorna erro; `dxcompiler` não é linkado | Os shaders já são DXIL pré-compilado. O DXC do Windows SDK assina o DXIL no build (o `dxil.dll` fica ao lado do `dxc.exe`), então não precisa empacotar `dxcompiler.dll`/`dxil.dll` | Empacotar as DLLs do DXC (risco de dependência desktop-only no console) |
| D6 | `CompileShaders.cmake` prefere o `dxc` do **Windows SDK** quando `WINDOWS_STORE` | Garante DXIL assinado; o `dxc` do Vulkan SDK pode não ter `dxil.dll` | — |
| D7 | Overlay **rtmidi** sem WinMM (backend dummy do RtMidi) | O código de MIDI do Ymir continua igual; WinMM não existe pra apps UWP | `#if` em todo o código de MIDI |
| D8 | Overlay **tomlplusplus** header-only (`TOML_HEADER_ONLY=0` + `toml_implementation.cpp` do app) | O port upstream usa meson, que não suporta WindowsStore (`supports: !uwp`) | Vendorizar `toml.hpp` |
| D9 | Opções `Ymir_ENABLE_DISCORD_RPC` e `Ymir_ENABLE_UPDATE_CHECKER` (OFF no UWP) | Discord usa named pipes e registro; o update checker precisa de curl + OpenSSL | Portar curl/OpenSSL pra UWP |
| D10 | Assinatura com **`CN=CMake Test Cert`** (o `.pfx` vem dos templates do CMake) por padrão | Zero configuração: o build já gera `.msix` assinado e `.cer`. `Ymir_UWP_PUBLISHER`/`Ymir_UWP_CERTIFICATE` permitem um certificado próprio | Gerar certificado com `New-SelfSignedCertificate` em todo setup |
| D11 | Perfil: `X:\Ymir` gravável (opt-in, a pasta precisa existir) ou LocalState | A pasta de instalação é só leitura; no console não cabe diálogo de primeira execução; USB facilita copiar BIOS e jogos | Só LocalState (ruim pra colocar BIOS) |
| D12 | Pickers WinRT próprios (`platform::uwp::ShowFileDialog`) | O build CMake do SDL3-uwp não compila `src/dialog/winrt`, e mesmo essa versão só abre um arquivo e ignora filtros | — |
| D13 | Capabilities `runFullTrust`, `broadFileSystemAccess`, `removableStorage`, `expandedResources`; **sem** `codeGeneration` e sem rede | Interpretador (sem JIT); acesso a caminhos arbitrários; modo jogo | — |
| D14 | `/sdl-` no UWP | Projetos WindowsStore do VS ligam `/sdl`, que transforma C4996 em erro no código vendorizado (stb) | `_CRT_SECURE_NO_WARNINGS` |
| D15 | `AdapterID` no UWP = índice de enumeração DXGI | D3DKMT (endereço PCIe) é desktop-only; o Xbox tem uma GPU só | — |
| D16 | AVX2 ligado no preset UWP | O Zen 2 do Series X|S suporta. O **Xbox One (Jaguar) não suporta AVX2** e também não tem D3D12 em modo App; fica fora do MVP | Preset SSE2 (dá pra adicionar depois) |
| D17 | Enhanced barriers forced off on UWP (graphics context and VDP HW renderer), legacy `ResourceBarrier` used instead | XBSX2 hard-disables them on Xbox ("Not supported on Xbox", `pcsx2/GS/Renderers/DX12/GSDevice12.cpp`) regardless of the feature check, and Ymir's graphics context issues them on every frame for the back buffer. The legacy path already exists and is what older desktop GPUs use | Trusting `EnhancedBarriersSupported` |
| D18 | CoreWindow swap chain without the frame latency waitable object, without `SetMaximumFrameLatency`, without tearing, and with `DXGI_SCALING_STRETCH` | Matches the swap chains known to work on Xbox: SDL3-uwp's own D3D12 renderer drops the waitable flag, the latency call and tearing on WinRT; RetroArch defaults waitable swap chains off on WinRT; xenia, Dolphin, XBSX2 and Cemu use STRETCH (xenia to fix a zoom bug). Ymir never waited on the waitable object anyway | The desktop settings (`SCALING_NONE`, waitable flag, tearing when supported) |
| D19 | D3D12 debug layer does not break into the debugger on UWP; messages are only printed | Under F5 a single first-frame validation warning paused the app with a black screen. No Xbox port breaks on warnings by default: xenia/Dolphin/XBSX2 make breaking opt-in, RetroArch and SDL3-uwp break on ERROR only | Break on WARNING/ERROR/CORRUPTION as on desktop |
| D20 | **Backend de áudio próprio no UWP: XAudio2** (`xaudio2_backend.{hpp,cpp}`), atrás da interface `AudioBackend` | O path WASAPI do SDL3 **trava** no Xbox: `SDL_OpenAudioDeviceStream` chama `ActivateAudioInterfaceAsync` e espera num semáforo sem timeout (`vendor/SDL3-uwp/src/audio/wasapi/SDL_wasapi_winrt.cpp:306` e `:317`). Bombear o dispatcher do CoreWindow (`SDL_PumpEvents`, ~5000 iterações em 10 s) **não** destrava. XAudio2 é síncrono, é a API nativa do console e não tem dependência de message pump | Manter o WASAPI do SDL e esperar o dispatcher; fazer o SDL abrir o áudio (proibido: `vendor/SDL3-uwp` não é alterado) |
| D21 | **Redirecionamento de stdout/stderr no nível do descritor**, nunca com `freopen` | O CRT da Microsoft **fecha a stream** quando o `freopen` falha; depois disso `fileno()` devolve `-1` e o `_dup2` no destrutor dispara um assert do CRT (`dup2.cpp:67`). Foi o que travou o boot no Xbox, em `util::StdioSuppressor`. A correção faz `dup` + `open("NUL")` + `dup2`, cacheia o fd alvo e nunca fecha a stream. Efeito extra: o descritor do pipe do log não é mais fechado, então a thread que drena o log sobrevive | `freopen` (quebra o boot); `SetStdHandle` (não cobre o CRT) |
| D22 | **Navegação da GUI pelo gamepad com o modelo nativo do ImGui**, em vez de um cursor virtual | O ImGui 1.93 já traz a navegação de console (d-pad, stick, A/B/X/Y, Start/Back, L1/R1) e o backend `imgui_impl_sdl3` já alimenta o ImGui com esses botões; o único bloqueio era a própria app, que ligava `NavEnableGamepad` só quando `io.NavActive` — um deadlock lógico, porque `io.NavActive` só vira true por gamepad se a flag já estiver ligada. Um cursor virtual exigiria injetar eventos de mouse sintéticos e ainda assim não resolve hover/drag em widget | Cursor virtual pelo analógico (design em `docs/uwp/TESTING.md` §5.3); manter o deadlock lógico |
| D23 | **Roteamento do gamepad entre a GUI e o Saturn**: modal aberto → GUI; senão → Saturn; botão View alterna | O console tem **um** gamepad para os dois consumidores. Sem rotear, cada aperto de botão na GUI também aperta o botão do jogo. O sinal de "modal aberto" é o `io.WantCaptureKeyboard`, que só é confiável porque a build UWP **desliga** a navegação por teclado (não há teclado): assim ele é true exatamente enquanto há modal aberto ou um widget em interação. O `View` não faz parte do Control Pad do Saturn, então está livre | Deixar os dois consumirem o mesmo gamepad (ações espelhadas no jogo); obrigar o mouse (impossível no console) |
| D24 | **Interface própria para o Xbox, a "Shell"** (`src/platform/uwp/shell/`), temática da BIOS do Saturn, com a GUI desktop mantida como modo avançado | A GUI desktop é menu bar + janelas pensadas para mouse; adaptá-la exigiria mexer no código mais ativo do upstream (`app.cpp`, `ui/`). A Shell fica inteira em diretórios do port, presa por ganchos de 1 a 3 linhas sob `YMIR_PLATFORM_UWP`, usa a navegação nativa do ImGui (D22) sobre widgets desenhados à mão e reaproveita os eventos/serviços do Ymir. Config em `shell.toml` próprio, para não tocar em `settings.cpp`. Arquitetura e decisões S1–S9 em [shell/README.md](shell/README.md) | Readaptar a GUI desktop no lugar; portar o `ImGuiFullscreen` do XBSX2 (tema próprio brigaria com ele) |

## 5. Macros e opções de CMake

| Nome | Onde | Valor no UWP | Efeito |
|---|---|---|---|
| `WINDOWS_STORE` | CMake (automático com `CMAKE_SYSTEM_NAME=WindowsStore`) | TRUE | Liga tudo abaixo |
| `Ymir_PLATFORM_UWP` | `CMakeLists.txt` raiz | ON | Variável CMake derivada de `WINDOWS_STORE` |
| `YMIR_PLATFORM_UWP` | macro C++ PUBLIC do `ymir-core` | 1 | Guardas no código C++ |
| `Ymir_ENABLE_DISCORD_RPC` | opção CMake / macro privada do `ymir-sdl3` | OFF | Remove discord-rpc |
| `Ymir_ENABLE_UPDATE_CHECKER` | opção CMake / macro privada do `ymir-sdl3` | OFF | Usa `update_checker_null.cpp`, sem curl/OpenSSL |
| `Ymir_UWP_PACKAGE_NAME` | cache, `UWPPackaging.cmake` | `Ymir.SaturnEmulator.UWP` | Identity Name do pacote |
| `Ymir_UWP_PUBLISHER` | cache, `UWPPackaging.cmake` | `CN=CMake Test Cert` | Precisa ser igual ao subject do certificado |
| `Ymir_UWP_CERTIFICATE` | cache, `UWPPackaging.cmake` | vazio | `.pfx` próprio (opcional, sem senha) |
| `Ymir_UWP_PACKAGE_VERSION` | cache, `UWPPackaging.cmake` | vazio = `<versão do Ymir>.0` | Versão do pacote, com 4 números (CI: tag ou run) |
| `MIO_WINAPI_APP` | `vendor/mio/include/mio/detail/mmap.ipp` | 1 (automático) | APIs `*FromApp` no mio |

## 6. Mapa das mudanças (o delta do fork)

### Build e configuração
- `CMakeLists.txt`:
  - pula `find_package(Vulkan)` no UWP;
  - define `Ymir_PLATFORM_UWP`, `Ymir_ENABLE_DISCORD_RPC` e `Ymir_ENABLE_UPDATE_CHECKER`;
  - adiciona `/sdl-`.
- `CMakePresets.json`: inclui `cmake/presets/uwp-x86_64-AVX2-msvc.json` (novo), com os presets
  `UWP-x86-64-AVX2-MSVC` (VS 2026) e `UWP-x86-64-AVX2-MSVC-VS2022` (fallback). A config base é
  `cmake/config/platform-uwp.json` (novo).
- `cmake/CompileShaders.cmake`: no UWP usa o DXC do Windows SDK. Também ganhou fallback pro SDK quando o `dxc` não está no PATH.
- `vcpkg.json`: `curl`, `openssl`, `sdl3` e `catch2` com `"platform": "!uwp"`.
- `vcpkg-overlay-ports/uwp/{rtmidi,tomlplusplus}` (novos), usados só pelo preset UWP (`VCPKG_OVERLAY_PORTS`).
- `vendor/CMakeLists.txt`: discord-rpc só quando `Ymir_ENABLE_DISCORD_RPC`.
- `.gitmodules`:
  - novo submódulo `vendor/SDL3-uwp` (SternXD, branch `uwp-3.4.16`, shallow);
  - **`vendor/mio` precisa apontar pro fork com o patch UWP** (ver HANDOFF, passo 1).
- `.gitignore`: `scripts/uwp/xbox.env` e `*.pfx`.

### ymir-core
- `libs/ymir-core/CMakeLists.txt`:
  - macro `YMIR_PLATFORM_UWP`;
  - no UWP compila `gpu_shaders_uwp.cpp` no lugar de `gpu_shaders_windows.cpp`;
  - no UWP não linka `synchronization` nem `dxcompiler`.
- `src/ymir/util/virtual_memory.cpp`: `*FromApp` no UWP; `#ifdef WIN32` virou `_WIN32`.
- `src/ymir/util/thread_name.cpp`:
  - no UWP chama `SetThreadDescription` direto;
  - **bugfix genérico:** o ponteiro ficava nulo quando `GetProcAddress` falhava, e `LoadLibrary` virou `LoadLibraryA`.
- `include/ymir/gpu/d3d12/d3d12_swap_chain.hpp`:
  - novo `CreateForCoreWindow()`, com a lógica comum em `CreateImpl()`;
  - `SetFullscreenState` só em swap chain HWND.
  - on UWP, CoreWindow swap chains have no tearing, and the waitable object is optional (D18).
- `src/ymir/hw/vdp/renderer/vdp_renderer_hw_d3d12.cpp`: enhanced barriers forced off on UWP (D17).
- `src/ymir/gpu/shaders/gpu_shaders_uwp.cpp` (novo).
- `vendor/mio/include/mio/detail/mmap.ipp` (submódulo): ramos `MIO_WINAPI_APP`.

### Frontend (ymir-sdl3)
- `apps/ymir-sdl3/CMakeLists.txt`:
  - fontes de `platform/uwp`;
  - `update_checker.cpp` ou `update_checker_null.cpp`, conforme a opção;
  - links condicionais (discord, curl, Dwmapi);
  - sem `ymir.rc` e sem `WIN32_EXECUTABLE` no UWP;
  - `include(uwp/UWPPackaging.cmake)`.
- `apps/ymir-sdl3/uwp/` (novo): `UWPPackaging.cmake`, `Package.appxmanifest.in`, `Assets/*.png` (gerados a partir de `res/ymir.png`).
- `src/main.cpp`: inclui `SDL_main.h`, cria o argv falso e liga o log em arquivo, tudo só no UWP.
- `src/winmain.cpp`: desligado no UWP.
- `src/platform/uwp/uwp_platform.{hpp,cpp}` (novo): log, perfil e pickers.
- `src/platform/uwp/stall_watchdog.{hpp,cpp}` (new, temporary): logs the main-loop phase the UI thread is stuck in.
- `src/platform/uwp/shell/` (novo): a Shell do Xbox (D24). Assets embutidos em `uwp/shell/assets/` via
  `uwp/ShellAssets.cmake`. Ganchos guardados em `app.hpp`/`app.cpp`, listados em
  [shell/README.md](shell/README.md) §3. Testes da lógica pura em `tests/ymir-sdl3-tests/src/platform/uwp/shell/`.
- `src/app/app.cpp`: seleção de perfil no UWP.
- `src/app/services/file_dialog_service.cpp`: desvia pros pickers WinRT.
- `src/app/services/gfx/gfx_context_impl_d3d12.cpp`: CoreWindow + `SDL_GetWindowSizeInPixels`.
- `src/app/services/gfx/gfx_d3d_utils.cpp`: sem D3DKMT no UWP.
- `src/app/services/discord_rpc_service.cpp`: no-op sem `Ymir_ENABLE_DISCORD_RPC`.
- `src/app/update_checker.hpp` e `src/app/update_checker_null.cpp` (novo).
- `src/app/ui/windows/about_window.cpp`: entradas de curl, OpenSSL, nghttp* e brotli só com update checker.
- `src/util/os_exception_handler.cpp`: no-op no UWP (Vectored Exception Handler é desktop-only).
- `src/util/os_features.cpp`: sem DWM no UWP.

### Scripts e docs
- `scripts/uwp/common.ps1`: detecta VS 2026/2022 e acha o pacote gerado.
- `scripts/uwp/open-vs.ps1`: gera e abre a solution (`out/build/<preset>/Ymir.slnx`) no Visual Studio.
- `scripts/uwp/build-deps.ps1`, `build.ps1` (`-PackageVersion`), `package.ps1` (zip distribuível),
  `deploy-xbox.ps1`, `xbox.env.example`.
- `.github/workflows/uwp.yml`: CI/CD (seção 3.2).
- `docs/uwp/README.md` (este arquivo) e `docs/uwp/HANDOFF.md`.

## 7. Candidatos a PR no upstream (reduzem o delta do fork)

1. Bugfix do `thread_name.cpp` (ponteiro nulo quando `GetProcAddress` falha; `LoadLibraryA`).
2. `#ifdef WIN32` → `_WIN32` em `virtual_memory.cpp`.
3. Opções `Ymir_ENABLE_DISCORD_RPC` e `Ymir_ENABLE_UPDATE_CHECKER`, mais `update_checker_null.cpp`. Úteis pra distros
   e builds mínimos.
4. Fallback do DXC pro Windows SDK em `CompileShaders.cmake`.
5. `D3D12SwapChain::CreateForCoreWindow` (genérico, sem dependência de UWP).

## 8. Referências no workspace

| Assunto | Arquivo |
|---|---|
| Receita geral de port UWP | `D:\workspace\_non_work_\xbox-uwp-lab\docs\port-recipe.md` |
| Limitações do Dev Mode (W^X, memória, watchdog, debugger) | `xbox-uwp-lab\docs\devmode-limitations.md` |
| Filesystem (capabilities, `*FromApp`, LocalState) | `xbox-uwp-lab\docs\filesystem.md` |
| Assinatura e Device Portal | `xbox-uwp-lab\docs\deploy.md` |
| Dossiê original do Ymir | `xbox-uwp-lab\docs\targets\ymir.md` |
| App vs Game (1 GB vs 5 GB, DX12 só em Game no Xbox One) | `scummvm-uwp\docs\XBOX-TILE-LAUNCH-KILL.md` |
| USB NTFS com ACL pra apps | `xb-homebrew-vault\docs\investigations\usb-ntfs-xbox-prepare.md` |
| Ferramenta de deploy com GUI | `xb-homebrew-vault` (Device Portal, SFTP, logs) |
| Manifest de referência | `XBSX2\pcsx2-uwp\Package.appxmanifest` |
| Picker WinRT em outro emulador | `dolphin\Source\DolphinWinRT\UWPUtils.h` |
