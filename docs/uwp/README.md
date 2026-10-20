# Ymir para UWP / Xbox Dev Mode — Arquitetura e decisões

> Documento de referência do port. O passo a passo de execução fica em [HANDOFF.md](HANDOFF.md).
> Última atualização: 2026-10-20.

## 1. Objetivo

Rodar o frontend `ymir-sdl3` do Ymir (emulador de Sega Saturn) como app **UWP** num **Xbox Series X|S em Dev Mode**,
usando o **renderer VDP por hardware em Direct3D 12**. O mesmo pacote também roda no Windows 10/11 como app UWP, o
que facilita testar antes de ir pro console.

## 2. Estratégia de repositório

- **Fork:** `marcelofrau/Ymir`, branch **`uwp/main`**. O upstream é `ymir-emu/Ymir` (remote `upstream`).
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

### 3.2 Fluxo de runtime no UWP

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
| `Ymir_UWP_CERTIFICATE` | cache, `UWPPackaging.cmake` | vazio | `.pfx` próprio (opcional) |
| `MIO_WINAPI_APP` | `vendor/mio/include/mio/detail/mmap.ipp` | 1 (automático) | APIs `*FromApp` no mio |

## 6. Mapa das mudanças (o delta do fork)

### Build e configuração
- `CMakeLists.txt`:
  - pula `find_package(Vulkan)` no UWP;
  - define `Ymir_PLATFORM_UWP`, `Ymir_ENABLE_DISCORD_RPC` e `Ymir_ENABLE_UPDATE_CHECKER`;
  - adiciona `/sdl-`.
- `CMakePresets.json`: inclui `cmake/presets/uwp-x86_64-AVX2-msvc.json` (novo). A config base é `cmake/config/platform-uwp.json` (novo).
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
- `src/app/app.cpp`: seleção de perfil no UWP.
- `src/app/services/file_dialog_service.cpp`: desvia pros pickers WinRT.
- `src/app/services/gfx/gfx_context_impl_d3d12.cpp`: CoreWindow + `SDL_GetWindowSizeInPixels`.
- `src/app/services/gfx/gfx_d3d_utils.cpp`: sem D3DKMT no UWP.
- `src/app/services/discord_rpc_service.cpp`: no-op sem `Ymir_ENABLE_DISCORD_RPC`.
- `src/app/update_checker.hpp` e `src/app/update_checker_null.cpp` (novo).
- `src/app/ui/windows/about_window.cpp`: entradas de curl, OpenSSL, nghttp* e brotli só com update checker.
- `src/app/ui/views/settings/video_settings_view.cpp`: toggle "Use hardware acceleration" visível no UWP.
- `src/util/os_exception_handler.cpp`: no-op no UWP (Vectored Exception Handler é desktop-only).
- `src/util/os_features.cpp`: sem DWM no UWP.

### Scripts e docs
- `scripts/uwp/build-deps.ps1`, `build.ps1`, `deploy-xbox.ps1`, `xbox.env.example`.
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
