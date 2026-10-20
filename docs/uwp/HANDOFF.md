# HANDOFF: Ymir UWP / Xbox Dev Mode (runbook sequencial)

> **Para o agente que vai continuar:** este documento é um roteiro **sequencial**. Execute os passos **na ordem**,
> um de cada vez, e marque `[x]` ao concluir. A arquitetura e o motivo de cada decisão estão em
> [README.md](README.md). Leia a seção 0 antes de começar.
>
> Escrito em 2026-10-20 na máquina de origem (Build Tools 2026, sem Xbox). Branch: `uwp/main` do fork `marcelofrau/Ymir`.

---

## 0. Regras para o agente executor

1. **Siga a ordem.** Cada passo depende do anterior. Não pule passos e não junte passos.
2. **Não refatore nem "melhore"** código que já funciona. O objetivo agora é validar no Xbox e corrigir só o que
   quebrar.
3. **Toda mudança em código compartilhado** (fora de `platform/uwp/`, `apps/ymir-sdl3/uwp/` e `scripts/uwp/`)
   precisa ficar dentro de `#if YMIR_PLATFORM_UWP ... #endif` (C++) ou `if (Ymir_PLATFORM_UWP)` (CMake), para não
   quebrar o build desktop.
4. **Siga o estilo do código ao redor:** indentação de 4 espaços, diretivas de pré-processador aninhadas indentadas
   (`    #include`), comentários em inglês, line endings LF.
5. **Quando um passo falhar:** procure o sintoma na **seção 4 (Troubleshooting)** e aplique a receita. Se não estiver
   lá, **pare e registre** o erro completo (comando, saída, arquivo de log) na seção 5 antes de tentar qualquer coisa
   criativa.
6. **Nunca rode `rm -rf`/`Remove-Item -Recurse` em diretórios do repo.** Para recomeçar um build, use
   `cmake --fresh` (os scripts aceitam `-Reconfigure`/`-Clean`).
7. **Não faça commit de** `scripts/uwp/xbox.env`, `*.pfx`, nem nada dentro de `out/`.
8. Commits pequenos, um por correção, mensagem no formato `uwp: <o que mudou>`.

---

## 1. Estado atual (o que já está pronto e validado)

Validado na máquina de origem em 2026-10-20 (VS Build Tools 2026 18.x, MSVC 14.51, Windows SDK 10.0.26100, CMake 4.3.2):

| Item | Estado |
|---|---|
| Fork e branch `uwp/main`, remotes `origin` (fork) e `upstream` (ymir-emu) | ✅ |
| Submódulo `vendor/SDL3-uwp` (SternXD, `uwp-3.4.16`) | ✅ |
| `scripts/uwp/build-deps.ps1` → `out/uwp-deps/bin/SDL3.dll` | ✅ compila |
| Dependências vcpkg em `x64-uwp-static-md` (com overlays `rtmidi` e `tomlplusplus`) | ✅ instalam |
| `ymir-core` em WindowsStore (incluindo o renderer HW D3D12) | ✅ compila |
| `ymir-sdl3` em WindowsStore, **Debug** e **RelWithDebInfo** | ✅ compila e linka |
| `.msix` assinado (`CN=CMake Test Cert`) + `.cer` + `Dependencies\x64\Microsoft.VCLibs*.appx` | ✅ gerados |
| Conteúdo do `.msix`: `ymir-sdl3.exe`, `SDL3.dll`, `Assets\*.png`, `resources.pri`, `AppxSignature.p7x` | ✅ conferido |
| Shaders DXIL assinados (hash do container DXBC diferente de zero) | ✅ conferido |
| Build desktop (preset `Full-x86-64-AVX2-MSVC-Release`) e testes continuam passando | ✅ 75/75 (seção 1.1) |
| `scripts/uwp/package.ps1` → `out/dist/ymir-uwp_<ver>_x64.zip` (msix, cer, VCLibs, Install.ps1, INSTALL.md) + zip de símbolos | ✅ gerado e conferido |
| CI/CD `.github/workflows/uwp.yml` (build, artifact, release por tag `uwp-v*`) | ✅ primeiro run verde em 2026-10-20 (runner `windows-2025` com **Visual Studio Enterprise 2026**, versão 0.4.0.2, artifacts `ymir-uwp-0.4.0.2` e `-symbols`). A publicação de release por tag ainda não foi exercitada (passo 15) |
| Fallback VS 2022 (preset `UWP-x86-64-AVX2-MSVC-VS2022`) | ⚠️ configurado, mas **não compilado**: a máquina de origem e o runner do GitHub têm VS 2026 |
| **Rodar o app** (PC ou Xbox) | ❌ **não testado**: a máquina de origem não tem Xbox e é corporativa |

### 1.1 Resultado da regressão desktop
✅ Em 2026-10-20, com o código final desta branch: preset `Full-x86-64-AVX2-MSVC-Release` (Ninja + MSVC 14.51 +
triplet `x64-windows`) compilou sem erros, e o `ctest` deu **75/75 testes OK**. As mudanças do port não quebram o
desktop.

### 1.2 Identidade do pacote (útil para achar arquivos)

| Campo | Valor |
|---|---|
| Identity Name | `Ymir.SaturnEmulator.UWP` |
| Publisher | `CN=CMake Test Cert` |
| **Package Family Name (PFN)** | `Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm` |
| App Id | `App` (PRAID = `Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm!App`) |
| Versão | `0.4.0.0` local (vem do `project(Ymir VERSION ...)` no CMake); no CI, `0.4.0.<run>` ou a da tag `uwp-v*` |
| Pasta de dados (PC) | `%LOCALAPPDATA%\Packages\Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm\LocalState\StrikerX3\Ymir\` |
| Pasta de dados (Xbox, Device Portal → File explorer) | `LocalAppData\Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm\LocalState\StrikerX3\Ymir\` |
| **Log** | `<pasta de dados>\ymir-uwp.log` (stdout e stderr, com flush por linha) |

> Se o `Ymir_UWP_PUBLISHER` for trocado, o PFN muda. O sufixo é um hash do Publisher.

---

## 2. Pré-requisitos da máquina de destino

Confira cada item **antes** do passo 1. Os comandos de verificação devem rodar no PowerShell 7 (`pwsh`).

- [ ] **Visual Studio 2026** (ou Build Tools 2026, versão 18.x) com:
  - workload **Desktop development with C++** (MSVC v145, x64);
  - workload **Universal Windows Platform development** com o componente **C++ (v145) Universal Windows Platform tools**;
  - **Windows 11 SDK 10.0.26100**.

  Verificação:
  `& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property displayName`
  precisa mostrar algo com "2026". Também precisa existir a pasta
  `<instalação do VS>\MSBuild\Microsoft\VisualStudio\v18.0\AppxPackage`.
- [ ] **CMake ≥ 3.28** com suporte ao gerador "Visual Studio 18 2026". Verificação: `cmake --help` lista
  `Visual Studio 18 2026`. O CMake que vem com o VS 2026 serve.
- [ ] **Git** (com acesso ao GitHub) e **PowerShell 7** (`pwsh --version` ≥ 7).
- [ ] **DXC do Windows SDK**: `Test-Path "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe"` retorna `True`.
- [ ] (Para o passo 7, opcional) **Modo de desenvolvedor do Windows** ligado: Configurações → Sistema → Para desenvolvedores.
- [ ] (Para os passos 8 a 13) **Xbox Series X|S em Dev Mode**, na mesma rede do PC.

> Se só houver o VS 2022 (gerador "Visual Studio 17 2022"), veja **T-01** na seção 4.

---

## 3. Passo a passo

### Passo 1: Clonar e preparar o repositório
- [ ] Clone o fork e entre na branch:
  ```powershell
  git clone git@github.com:marcelofrau/Ymir.git
  cd Ymir
  git checkout uwp/main
  git remote add upstream https://github.com/ymir-emu/Ymir.git
  git submodule update --init --recursive
  ```
- [ ] Confira os submódulos:
  ```powershell
  git submodule status
  git -C vendor/mio log --oneline -1
  git -C vendor/SDL3-uwp log --oneline -1
  ```
  **Esperado:**
  - nenhuma linha de `git submodule status` começa com `-` (não inicializado) ou `+` (commit diferente);
  - `vendor/mio` mostra o commit de UWP (mensagem começando com `UWP:`);
  - `vendor/SDL3-uwp` mostra `ddd5d4f uwp: Minimal picker impl`.
- [ ] Confirme que o patch do mio está presente:
  `Select-String -Path vendor/mio/include/mio/detail/mmap.ipp -Pattern MIO_WINAPI_APP` precisa achar linhas.
  Se não achar, veja **T-02**.

### Passo 2: Bootstrap do vcpkg
- [ ] `.\vcpkg\bootstrap-vcpkg.bat -disableMetrics`
- [ ] **Esperado:** existe o arquivo `vcpkg\vcpkg.exe`.

### Passo 3: Compilar as dependências pré-compiladas (SDL3-uwp)
- [ ] `pwsh scripts/uwp/build-deps.ps1`
- [ ] **Esperado:** a última linha é `SDL3-uwp installed: ...\out\uwp-deps\bin\SDL3.dll`.
  Durante o configure, aparece `Video drivers: dummy offscreen winrt` e `Joystick drivers: hidapi virtual wgi`.
- [ ] Se falhar no configure com "could not find any instance of Visual Studio", veja **T-01**.

### Passo 4: Build Debug do pacote
- [ ] `pwsh scripts/uwp/build.ps1 -Configuration Debug`

  O primeiro configure demora alguns minutos: o vcpkg compila as dependências para `x64-uwp-static-md`.
- [ ] **Esperado** no fim:
  ```
  MSIX:         ...\AppPackages\ymir-sdl3\ymir-sdl3_0.4.0.0_x64_Debug_Test\ymir-sdl3_0.4.0.0_x64_Debug.msix
  Dependency:   ...\Dependencies\x64\Microsoft.VCLibs.x64.Debug.14.00.appx
  Certificate:  ...\ymir-sdl3_0.4.0.0_x64_Debug.cer
  ```
- [ ] Warnings **esperados e inofensivos**:
  - `APPX0006 ... runFullTrust`;
  - `LNK4075 ... /INCREMENTAL`;
  - os vários `compile_hlsl_shader: This generator does not support depfiles`.
- [ ] Qualquer `error` está na seção 4 (T-03 a T-07).

### Passo 5: Build Release do pacote
- [ ] `pwsh scripts/uwp/build.ps1 -Configuration Release`
- [ ] **Esperado:** `...\ymir-sdl3_0.4.0.0_x64_RelWithDebInfo_Test\ymir-sdl3_0.4.0.0_x64_RelWithDebInfo.msix` e
  `Dependencies\x64\Microsoft.VCLibs.x64.14.00.appx`.
- [ ] **Use o Release pro Xbox.** O Debug depende do `Microsoft.VCLibs.140.00.Debug` e é bem mais lento.

### Passo 6 (opcional, recomendado antes de mexer em código compartilhado): Regressão desktop
- [ ] Abra o **x64 Native Tools Command Prompt for VS 2026** e rode:
  ```bat
  cmake --preset Full-x86-64-AVX2-MSVC-Release
  cmake --build out\build\Full-x86-64-AVX2-MSVC-Release
  ctest --test-dir out\build\Full-x86-64-AVX2-MSVC-Release --output-on-failure -j 8
  ```
- [ ] **Esperado:** build sem erros e `100% tests passed`.

### Passo 7 (opcional, recomendado): Smoke test no PC (Windows como app UWP)
Achar problemas no PC é muito mais barato do que no Xbox.
- [ ] Na pasta `...\ymir-sdl3_0.4.0.0_x64_RelWithDebInfo_Test\`, rode `pwsh .\Install.ps1`.

  Ele pede elevação para instalar o `.cer` em *Trusted People* e instala as dependências. Se não puder elevar,
  veja **T-08**.
- [ ] Abra "Ymir" pelo menu Iniciar.
- [ ] **Esperado:** a janela abre e aparece a UI do Ymir (barra de menu ImGui).
- [ ] Abra o log `%LOCALAPPDATA%\Packages\Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm\LocalState\StrikerX3\Ymir\ymir-uwp.log`
  e confirme que existe e tem a linha `UWP profile path: ...`.
- [ ] Coloque uma BIOS do Saturn em `<pasta de dados>\roms\ipl\` (veja o passo 11).
- [ ] Teste `File > Load disc...`. **Esperado:** abre o seletor de arquivos do Windows (picker WinRT) e o jogo carrega.
- [ ] Ligue `Settings > Video > Use hardware acceleration`. **Esperado:** aparece a mensagem
  "Direct3D 12 renderer initialized successfully" e o jogo continua renderizando.
- [ ] Registre o resultado na seção 5.
- [ ] Para desinstalar:
  `Get-AppxPackage Ymir.SaturnEmulator.UWP | Remove-AppxPackage`.

### Passo 8: Preparar o Xbox
- [ ] O Xbox está em **Dev Mode** (app *Xbox Dev Mode* → Switch and restart).
- [ ] No **Dev Home**:
  - Remote Access Settings → **Enable Xbox Device Portal**;
  - defina usuário e senha;
  - anote o IP mostrado (ex.: `https://192.168.0.50:11443`).
- [ ] No PC, abra `https://<IP>:11443` no navegador, aceite o certificado e faça login. Isso confirma rede e credenciais.
- [ ] Copie `scripts/uwp/xbox.env.example` para `scripts/uwp/xbox.env` e preencha `XBOX_IP`, `XBOX_USER` e
  `XBOX_PASS`. Esse arquivo é ignorado pelo git.

### Passo 9: Instalar no Xbox
Escolha **uma** das opções:
- [ ] **A (script):** `pwsh scripts/uwp/deploy-xbox.ps1 -Configuration Release`
  - **Esperado:** `Installed.`
  - Se falhar, veja **T-09** e **T-10**.
- [ ] **B (navegador):**
  1. Device Portal → *Home* → *My games & apps* → **Add**.
  2. Selecione o `.msix` do Release → Next.
  3. Adicione o `Dependencies\x64\Microsoft.VCLibs.x64.14.00.appx` → Start.
- [ ] **C (GUI):** use o `xb-homebrew-vault`, que instala com as dependências.

### Passo 10: Configurar o app como **Game** (obrigatório)
- [ ] No Dev Home, selecione o tile do Ymir → botão **View** → **View details** → **App type: Game**.

  **Por quê:** o tipo App limita a **1 GB de RAM** e a uma fatia da GPU. O tipo Game dá 5 GB e a GPU completa.
  Esse limite **não vale sob o debugger**, então um teste com debugger engana. Detalhes em
  `scummvm-uwp\docs\XBOX-TILE-LAUNCH-KILL.md`.

### Passo 11: Colocar a BIOS (IPL ROM) no lugar
Escolha **uma** das opções:
- [ ] **A (USB, recomendado):**
  1. Num pendrive NTFS, crie a pasta **`Ymir`** na raiz.
  2. Dentro dela, crie `roms\ipl\` e copie a BIOS (ex.: `Sega Saturn BIOS v1.01 (JAP).bin`).
  3. Copie os jogos para qualquer pasta do pendrive.
  4. **A pasta `Ymir` precisa ter permissão para apps UWP.** No PC, rode como admin:
     `icacls E:\ /grant "*S-1-15-2-1:(OI)(CI)F" /T`, trocando `E:` pela letra do pendrive.
     Detalhes em `xb-homebrew-vault\docs\investigations\usb-ntfs-xbox-prepare.md`.

  No boot, o Ymir procura `D:\Ymir` até `Z:\Ymir` e usa o primeiro onde consegue gravar.
- [ ] **B (LocalState):** pelo Device Portal → *File explorer* → `LocalAppData\Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm\LocalState\StrikerX3\Ymir\`.
  Essa pasta só existe depois da primeira execução, então rode o app uma vez antes. Crie `roms\ipl\` e faça upload
  da BIOS.

### Passo 12: Primeira execução no Xbox
- [ ] Conecte **teclado e mouse USB** no Xbox. A navegação da UI só com controle ainda não está pronta; veja o
  backlog B-01.
- [ ] Inicie o Ymir **pelo tile do Dev Home**, não pelo debugger. Também serve `deploy-xbox.ps1 -Launch`.
- [ ] Leia o log pelo Device Portal → File explorer → `LocalAppData\Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm\LocalState\StrikerX3\Ymir\ymir-uwp.log`.
  Baixe o arquivo e anexe na seção 5.
- [ ] Preencha o checklist de verificação abaixo, marcando OK, FALHA ou N/A, e registre na seção 5:

| # | Verificação | Como conferir | Resultado |
|---|---|---|---|
| V1 | O app abre e fica aberto por mais de 1 minuto | Visual | |
| V2 | Ocupa a TV inteira, sem bordas pretas grandes | Visual. Se houver bordas, veja **T-13** | |
| V3 | A resolução no log é a da TV (ex.: 3840x2160 no Series X) | Log | |
| V4 | Áudio funciona (som de boot da BIOS) | Ouvir | |
| V5 | Controle funciona no jogo | Jogar | |
| V6 | `File > Load disc...` abre o picker e carrega um jogo do USB | Visual | |
| V7 | Hardware acceleration ligado: "Direct3D 12 renderer initialized successfully" | Mensagem na tela e log | |
| V8 | Velocidade de 100% (60 fps) com HW renderer | Barra de status ou overlay | |
| V9 | Save de jogo (backup RAM) persiste depois de fechar e abrir o app | Salvar, fechar, abrir | |
| V10 | Save state funciona | Menu | |
| V11 | Voltar pra Home do Xbox e retornar não trava (suspend/resume) | Botão Xbox | |
| V12 | Uso de memória abaixo de 1 GB (informativo) | Device Portal → Performance | |

### Passo 13: Corrigir o que falhou
- [ ] Para cada FALHA do passo 12, procure a receita na seção 4, aplique, recompile (passo 5), reinstale (passo 9) e
  repita a verificação.
- [ ] Faça um commit por correção (`uwp: ...`) e `git push origin uwp/main`.

### Passo 14: Gerar o distribuível local (mesmo formato do CI)
- [ ] `pwsh scripts/uwp/package.ps1 -Configuration Release`
- [ ] **Esperado:** `out\dist\ymir-uwp_<versão>_x64.zip` e `out\dist\ymir-uwp_<versão>_x64_symbols.zip`.
  O zip tem `.msix`, `.cer`, `Dependencies\x64\Microsoft.VCLibs.x64.14.00.appx`, `Install.ps1` e `INSTALL.md`.

### Passo 15: Ativar e validar o CI/CD no GitHub
- [ ] No GitHub, em `marcelofrau/Ymir` → **Actions**, habilite os workflows do fork (forks começam com Actions
  desligado).
- [ ] Opcional, para economizar minutos: `gh workflow disable CI -R marcelofrau/Ymir` e
  `gh workflow disable "PR builds" -R marcelofrau/Ymir`. Confira os nomes com `gh workflow list -R marcelofrau/Ymir`.
- [ ] Dispare um build: `gh workflow run uwp.yml -R marcelofrau/Ymir --ref uwp/main`. Acompanhe com
  `gh run watch -R marcelofrau/Ymir`.
- [ ] **Esperado:** o job "Build UWP package (x64)" termina verde e o run tem o artifact `ymir-uwp-0.4.0.<run>`.
  O passo "Show toolchain" mostra qual VS foi usado.
- [ ] Se falhar, baixe o log (`gh run view --log-failed -R marcelofrau/Ymir`) e procure na seção 4. Nos erros de
  compilação com VS 2022, veja T-01.
- [ ] **Release:** depois do teste no Xbox (passo 12), crie e envie a tag:
  ```powershell
  git tag uwp-v0.4.0.1
  git push origin uwp-v0.4.0.1
  ```
  **Esperado:** aparece um GitHub Release "Ymir UWP 0.4.0.1" com `ymir-uwp_0.4.0.1_x64.zip` e o zip de símbolos.
  Incremente o último número a cada release; ele precisa ser maior que o do release anterior pro Xbox aceitar como
  atualização.

### Passo 16: Backlog pós-MVP
- [ ] Só depois de V1 a V9 passarem, siga a seção 6, **em ordem**.

---

## 4. Troubleshooting (sintoma → causa → correção)

### T-01: O gerador "Visual Studio 18 2026" não existe ou não acha a instalação
- **Comportamento automático:** os scripts (`common.ps1`) usam o VS 2022 e o preset `UWP-x86-64-AVX2-MSVC-VS2022`
  quando o VS 2026 não está instalado ou o CMake é antigo. Para forçar, passe `-VisualStudio 2022` (ou `2026`) no
  `build-deps.ps1`/`build.ps1`, ou defina a variável de ambiente `YMIR_UWP_VS=2022`.
- **Com VS 2022**, é preciso ter o workload *Universal Windows Platform development* com o componente
  *C++ (v143) Universal Windows Platform tools*.
- **Se o build com VS 2022 falhar** e o VS 2026 não puder ser instalado, registre o erro na seção 5. Esse caminho
  ainda não foi validado.

### T-02: `vendor/mio` sem o patch UWP (erro `CreateFileW: identifier not found` em `mmap.ipp`)
- **Causa:** o submódulo aponta pro mio original (StrikerX3), não pro fork com o patch.
- **Correção:**
  1. Confira `.gitmodules`: o bloco `vendor/mio` deve ter `url = https://github.com/marcelofrau/mio.git` e `branch = uwp`.
  2. Rode `git submodule sync vendor/mio` e `git submodule update --init vendor/mio`.
  3. Se o fork não existir, o patch está documentado na seção 7; aplique à mão no `vendor/mio`.

### T-03: `error C4996: 'sprintf': This function or variable may be unsafe` (no stb ou em outros vendors)
- **Causa:** `/sdl` ligado.
- **Correção:** confira se o `CMakeLists.txt` raiz tem o bloco `if (Ymir_PLATFORM_UWP AND MSVC) add_compile_options(/sdl-)`
  e reconfigure (`build.ps1 -Reconfigure`).

### T-04: vcpkg falha em `tomlplusplus` ("is only supported on '!uwp'") ou erro de meson
- **Causa:** a overlay não foi aplicada.
- **Correção:** confira `VCPKG_OVERLAY_PORTS` em `cmake/config/platform-uwp.json` (aponta para
  `${sourceDir}/vcpkg-overlay-ports/uwp`) e reconfigure.

### T-05: `Could not locate SDL3.dll for the UWP package`
- **Correção:** rode o passo 3 (`build-deps.ps1`).

### T-06: `Could NOT find DXC`
- **Correção:** instale o Windows SDK 10.0.26100 (seção 2). O `CompileShaders.cmake` procura em
  `Windows Kits\10\bin\<versão do SDK>\x64\dxc.exe`.

### T-07: Erro novo de compilação "identifier not found" em API Win32 (ex.: `XyzA`/`XyzW`)
- **Causa:** a API é desktop-only, e os headers do SDK escondem ela na partição APP.
- **Correção padrão:**
  1. Procure no header do SDK (`Windows Kits\10\Include\10.0.26100.0\um\*.h`) a variante `...FromApp` ou `...2`
     (ex.: `CreateFile2FromAppW`, `CreateFileMappingFromApp`, `MapViewOfFileFromApp`, `LoadPackagedLibrary`).
  2. Use essa variante dentro de `#if YMIR_PLATFORM_UWP`.
  3. Se não houver equivalente e a funcionalidade não for essencial, transforme em no-op no UWP, como foi feito em
     `os_features.cpp` e `os_exception_handler.cpp`.

### T-08: Instalação no PC falha com 0x800B0109 (certificado não confiável)
- **Correção:** como admin, importe o `.cer` em `LocalMachine\TrustedPeople`:
  `Import-Certificate -FilePath <arquivo>.cer -CertStoreLocation Cert:\LocalMachine\TrustedPeople`.
- **Alternativa sem admin**, desde que o Modo de desenvolvedor esteja ligado: extraia o `.msix` e registre o layout.
  ```powershell
  $pkg = Get-ChildItem out\build\UWP-x86-64-AVX2-MSVC\apps\ymir-sdl3\AppPackages\ymir-sdl3\*RelWithDebInfo_Test | Select-Object -First 1
  Add-AppxPackage (Get-ChildItem "$($pkg.FullName)\Dependencies\x64\*.appx").FullName   # VCLibs
  Copy-Item (Get-ChildItem "$($pkg.FullName)\*.msix").FullName out\ymir-layout.zip
  Expand-Archive out\ymir-layout.zip -DestinationPath out\ymir-layout-1   # use um nome novo a cada vez
  Add-AppxPackage -Register out\ymir-layout-1\AppxManifest.xml
  ```
  A pasta `RelWithDebInfo\` do build **não** serve como layout, porque não tem os `Assets`.

### T-09: Deploy no Xbox falha com 0x80073CF3 ou "dependency not found"
- **Causa:** o `Microsoft.VCLibs` não foi enviado junto.
- **Correção:** use a opção B do passo 9 e adicione o `.appx` da pasta `Dependencies\x64`. O `deploy-xbox.ps1` já
  envia; confirme que ele listou `Dependency: ...` antes do upload.

### T-10: `deploy-xbox.ps1` retorna 401/403
- **Causas:** credenciais erradas, ou falta do header CSRF.
- **Correção:**
  1. Teste login no navegador.
  2. O script lê o cookie `CSRF-Token` de `GET /api/os/info` e reenvia em `X-CSRF-Token`. Se o Device Portal não
     devolver o cookie, mude para a opção B ou C do passo 9 e registre na seção 5.

### T-11: App abre e fecha imediatamente (no tile) e o log está vazio ou nem existe
- **Causas prováveis, em ordem:**
  1. **App type: App** em vez de Game (passo 10).
  2. Falta a dependência VCLibs (T-09).
  3. Crash antes de `main()`. O `SDL3.dll` precisa estar no pacote: confira com
     `tar -tf <msix>` ou abrindo o `.msix` como zip.
  4. Watchdog de ativação (~250 ms): a janela precisa ser ativada logo. O SDL3-uwp ativa no `SDL_CreateWindow`.
     Se o boot ficar pesado antes disso, mova a inicialização pesada para depois da criação da janela.
- **Diagnóstico:**
  - Device Portal → *Crash data* (ative "Crash dumps" para o pacote) e baixe o dump;
  - ou rode o mesmo pacote no PC (passo 7), onde dá pra usar o Visual Studio pra depurar.

### T-12: Log mostra "Failed to create swapchain" ou tela preta com o app aberto
- **Causa provável:** o CoreWindow não aceita `DXGI_SCALING_NONE`.
- **Correção:** em `apps/ymir-sdl3/src/app/services/gfx/gfx_context_impl_d3d12.cpp`, no `Init()`, troque a linha
  `swapChainDesc.Scaling = DXGI_SCALING_NONE;` por:
  ```cpp
  #if YMIR_PLATFORM_UWP
          swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
  #else
          swapChainDesc.Scaling = DXGI_SCALING_NONE;
  #endif
  ```
- **Outras causas:** o device D3D12 não foi criado (veja T-14), ou o tamanho veio 0x0 de `SDL_GetWindowSizeInPixels`.
  Nesse caso, logue `windowWidth`/`windowHeight` e, se for 0, use 1920x1080 como fallback.

### T-13: Bordas pretas em volta da imagem no Xbox (área segura da TV)
- **Já mitigado:** `main.cpp` chama `platform::uwp::UseFullCoreWindowBounds()`, que faz
  `ApplicationView::SetDesiredBoundsMode(UseCoreWindow)`.
- **Se ainda houver bordas:**
  1. Confira no log a resolução reportada (V3).
  2. Se for menor que a da TV, o problema está no cálculo de tamanho do SDL3-uwp (`WINRT_TVOutputSizeOverridesActive` em
     `vendor/SDL3-uwp/src/video/winrt/SDL_winrtvideo.cpp`). Registre na seção 5 e não altere o SDL3-uwp sem
     necessidade, porque ele é um submódulo de terceiros.
  3. O conteúdo importante deve ficar dentro dos 90% centrais da tela.

### T-14: "Failed to create Direct3D 12 VDP renderer: ..." (o HW renderer não inicializa)
- **Diagnóstico:** a mensagem diz qual etapa falhou (shader, root signature, recurso).
- **Causas e correções:**
  - **Xbox One em modo App:** não tem D3D12. Use o tipo Game (passo 10) ou um Series X|S.
  - **Falha ao criar pipeline/shader:** o DXIL precisa estar assinado. No PC de build, confira que o
    `out\build\...\libs\ymir-core\shaders\src\vdp\*.cso` começa com `DXBC` seguido de 16 bytes **diferentes de zero**:
    `Format-Hex <arquivo>.cso | Select-Object -First 2`. Se for zero, o `dxc` usado não é o do Windows SDK; veja
    `DXC_EXECUTABLE` em `out\build\UWP-x86-64-AVX2-MSVC\CMakeCache.txt`.
  - **Feature faltando** (ex.: operações int64 em shader): registre a mensagem. O renderer por software continua
    funcionando como fallback automático.

### T-15: O picker não abre ou não retorna nada
- **Causa:** o picker precisa rodar na UI thread, e o resultado chega pelo loop de eventos do SDL.
- **Diagnóstico:** confirme no log que a chamada sai da thread principal. `FileDialogService::InvokeFileDialog` é
  chamado pela UI do ImGui, na thread principal.
- **Correção alternativa:** se o picker do sistema for ruim no Xbox, use o backlog B-02 (navegador de arquivos ImGui).

### T-16: Não consegue ler arquivos do USB (`E:\...`) mesmo com o picker
- **Causa:** faltam ACLs pra apps UWP no drive.
- **Correção:** a do passo 11-A (`icacls ... S-1-15-2-1`).

### T-17: O botão B fecha ou minimiza o app
- **Já mitigado:** `app.cpp` liga `SDL_HINT_WINRT_HANDLE_BACK_BUTTON` antes do `SDL_Init`, então o SDL marca o
  `BackRequested` como tratado.
- **Se ainda acontecer:** confirme que o hint é definido **antes** do `SDL_Init` (procure
  `SDL_HINT_WINRT_HANDLE_BACK_BUTTON` em `apps/ymir-sdl3/src/app/app.cpp`) e registre na seção 5.

---

## 5. Registro de execução (preencha durante o trabalho)

| Data | Passo | Resultado | Notas / erro / log anexado |
|---|---|---|---|
| 2026-10-20 | 1–6 | OK (máquina de origem) | `.msix` Debug e Release sem erros; regressão desktop 75/75 |
| 2026-10-20 | 15 (parcial) | OK | Run 39396157010 do `uwp.yml` verde com VS 2026 no runner; artifact de 5,3 MB |
| | | | |

---

## 6. Backlog pós-MVP (em ordem de prioridade)

Cada item tem um critério de aceite. Faça um item por vez, com commit próprio.

- **B-01: UX só com controle.**
  - Atalho no controle (ex.: segurar View + Menu) para abrir e fechar a barra de menu do ImGui e ativar
    `ImGuiConfigFlags_NavEnableGamepad` com `io.NavActive`.
  - Ponto de partida: `apps/ymir-sdl3/src/app/app.cpp`, procure "Selectively enable gamepad navigation".
  - **Aceite:** dá pra carregar um jogo e mudar uma configuração sem teclado e mouse.
- **B-02: Navegador de arquivos ImGui** como alternativa ao picker, navegável por controle. Lista drives `D:` a `Z:`
  e as pastas.
  - **Aceite:** abrir um `.cue` do USB só com o controle.
- **B-03: Escala da UI na TV.** Em 4K, ajuste o `displayScale` ou o tamanho das fontes no UWP.
  - **Aceite:** texto legível a 2 m da TV.
- **B-04: Suspend/resume.** Tratar `SDL_EVENT_WILL_ENTER_BACKGROUND` / `SDL_EVENT_DID_ENTER_FOREGROUND`: pausar a
  emulação e salvar a backup RAM.
  - **Aceite:** V11 passa e o save persiste depois de o sistema fechar o app em background.
- **B-05: Certificado próprio.**
  1. Gere com `New-SelfSignedCertificate -Type Custom -Subject "CN=YmirUWP" -KeyUsage DigitalSignature -CertStoreLocation Cert:\CurrentUser\My -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3","2.5.29.19={text}")`.
  2. Exporte o `.pfx` (fora do repo).
  3. Passe `-DYmir_UWP_PUBLISHER="CN=YmirUWP" -DYmir_UWP_CERTIFICATE=<caminho>`.
  - **Atenção:** o PFN muda (seção 1.2).
- **B-06: Certificado próprio no CI.** O CI já existe (passo 15) e assina com `CN=CMake Test Cert`. Se o B-05 for
  feito, guarde o `.pfx` (sem senha) em base64 no secret `UWP_SIGNING_PFX_BASE64`. No `uwp.yml`, decodifique antes do
  build e passe `-DYmir_UWP_CERTIFICATE=...` e `-DYmir_UWP_PUBLISHER=...` (o `build.ps1` precisa de um parâmetro novo
  para repassar esses argumentos ao CMake).
- **B-07: PRs no upstream** com os itens da seção 7 do README.md, para reduzir o delta.
- **B-08: Xbox One.** Preset SSE2 (`Ymir_AVX2=OFF`), renderer por software e tipo Game obrigatório. Medir desempenho.
- **B-09: Atualizar o dossiê** `D:\workspace\_non_work_\xbox-uwp-lab\docs\targets\ymir.md` com as respostas (interpretador,
  D3D12) e os resultados do Xbox.

---

## 7. Referência: patch do mio (caso precise reaplicar)

Arquivo `vendor/mio/include/mio/detail/mmap.ipp`:
1. Logo depois de `# include <vector>` (dentro de `#ifdef _WIN32`), adicione:
   ```cpp
   # if defined(WINAPI_FAMILY_PARTITION) && !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
   #  define MIO_WINAPI_APP 1
   #  include <fileapifromapp.h>
   # endif
   ```
2. Nas duas funções `open_file_helper`, sob `#ifdef MIO_WINAPI_APP`, use:
   `::CreateFile2FromAppW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, nullptr)`.
3. Em `memory_map`, sob `#ifdef MIO_WINAPI_APP`:
   - `::CreateFileMappingFromApp(file_handle, 0, protect, static_cast<ULONG64>(max_file_size), 0)`;
   - `::MapViewOfFileFromApp(file_mapping_handle, access, static_cast<ULONG64>(aligned_offset), length_to_map)`.

Mantenha os ramos `#else` com o código original.

## 8. Atualizar com o upstream

```powershell
git fetch upstream
git rebase upstream/main      # resolva conflitos mantendo os blocos #if YMIR_PLATFORM_UWP
git submodule update --init --recursive
pwsh scripts/uwp/build.ps1 -Configuration Release -Reconfigure
```

Conflitos mais prováveis:
- `apps/ymir-sdl3/CMakeLists.txt` (lista de fontes);
- `gfx_context_impl_d3d12.cpp` (o upstream mexe muito nele);
- `libs/ymir-core/CMakeLists.txt` (lista de shaders e fontes).

Depois do rebase, refaça os passos 4, 5 e 6.
