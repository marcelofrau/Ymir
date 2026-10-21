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
9. **Nunca faça deploy nem inicie o app** (PC ou Xbox). Nada de `deploy-xbox.ps1`, `Add-AppxPackage`, `Install.ps1`
   nem anexar debugger. O agente **edita e compila**; **o usuário** roda o F5 no Visual Studio 2026 direto no Xbox e
   devolve o log. Espere o relatório dele antes de continuar.
10. **Nunca faça `git commit` nem `git push` sem pedido explícito** do usuário.
11. **Tudo é escrito em inglês** — comentários, Doxygen, mensagens de commit e documentação. Só a conversa com o
    usuário é em português. Comente a lógica não óbvia; não deixe nada implícito.

---

## 0.1 Registro de passagem entre agentes (leia antes de tudo)

> Este handoff é o **canal de comunicação entre agentes**. Cada agente que trabalha no port registra aqui o que leu,
> o que fez, o que provou e o que deixou pendente para o próximo. **Não apague entradas antigas** — anexe as novas.

### Entrada do agente OpenCode — 2026-10-20 (sessão da máquina de destino, com Xbox)

**Escopo lido:** seções 0, 1, 4 (T-01..T-18), 5, 6. Branch `uwp/main`, HEAD `39aa6355`.

**Contexto:** esta sessão roda na **máquina de destino**, com Xbox em Dev Mode e VS 2026. O usuário compila pelo agente
e testa por F5 no VS 2026 direto no console.

**O que foi validado empiricamente nesta sessão:**

1. **O crash de `SDL_GetCurrentDirectory()` está resolvido.** O commit `39aa6355` (fix já existente no branch, alinhado
   ao diagnóstico do agente anterior) fez o app **sair do splash-crash e entrar no `main()`** no Xbox. Provado com
   marcadores de boot no Output do VS.
2. **A ordem de boot no Xbox está confirmada até o `App::Run()`:**

   ```
   [Ymir boot] main() entered
   [Ymir boot] log redirection done: Q:\Users\UserMgr2\AppData\Local\Packages\Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm\LocalState\StrikerX3\Ymir\ymir-uwp.log
   [Ymir boot] core window bounds set
   [Ymir boot] App::Run() entered
   ```

   Ou seja: `main()`, `RedirectStdioToLogFile()`, `UseFullCoreWindowBounds()` e a **entrada** do `App::Run()` todos
   executam. **O novo sintoma é: a tela fica no splash e o app não sai do `App::Run()` inicial** (não trava, não
   crasha — o log do VS mostra desligamento limpo com `thread exited with code 0`).

**Suspeitas levantadas (NÃO confirmadas — precisam de dado):**

- **T-12 tem a receita errada ou incompleta.** O documento manda trocar `DXGI_SCALING_NONE` por `DXGI_SCALING_STRETCH`
  sob UWP. Duas correções ao documento:
  1. O `#if YMIR_PLATFORM_UWP` que existe em `gfx_context_impl_d3d12.cpp` (~linha 319) é do `CreateForCoreWindow`,
     **não** do `Scaling` — a linha do `Scaling` continua incondicional. Então a receita **nunca foi aplicada**.
  2. A doc da Microsoft **não proíbe** `DXGI_SCALING_NONE` com `CreateSwapChainForCoreWindow` (o exemplo oficial de
     foreground layer usa `SCALING_NONE`). Então "o CoreWindow não aceita `SCALING_NONE`" **não está provado** — pode
     ser outra causa (ex.: tamanho 0x0).
- **Hipótese adicional em `d3d12_swap_chain.hpp` (~linha 102):** o código adiciona `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING`
  quando `CheckFeatureSupport` diz que há suporte. Tearing é descrito como coisa de janela HWND; se o Xbox reportar
  suporte, o flag entra numa swapchain de CoreWindow. **Não há proibição explícita na doc** — é hipótese, não fato.

**Instrumentação adicionada (para o próximo run revelar a causa exata):**

- `libs/ymir-core/include/ymir/gpu/d3d12/d3d12_swap_chain.hpp`: `Create`/`CreateForCoreWindow` guardam o `HRESULT` do
  DXGI e expõem `LastCreateHResult()`. **Comportamento idêntico** ao anterior (diff +3/-2 no `Create`, o
  `MakeWindowAssociation` continua lá).
- `apps/ymir-sdl3/src/app/services/gfx/gfx_context_impl_d3d12.cpp`: ao falhar a swapchain, agora loga `HRESULT`,
  `size WxH`, `scaling`, `flags`, `swapEffect`, `bufferCount`.
- `apps/ymir-sdl3/src/main.cpp` e `app.cpp`: marcadores de boot crus via `OutputDebugStringA`. **São temporários** —
  remover quando o boot estiver estável.
- `apps/ymir-sdl3/src/platform/uwp/uwp_platform.cpp`: o log agora espelha para o Output do VS via
  `OutputDebugStringA` (pipe + thread pump) e tem diagnóstico passo a passo do próprio redirect.

**Pendências / próximo passo (para o próximo agente):**

1. **Coletar o Output de um run com a instrumentação atual.** Em especial: a linha
   `error | GfxContext | Failed to create swapchain (CoreWindow): HRESULT ...` — ela sozinha decide qual dos
   candidatos é o culpado. Se ela **não** aparecer, o boot para antes do gfx e a investigação muda de lugar.
2. **Verificar se o espelhamento do log funciona.** Nesta sessão os marcadores de boot apareceram no Output, mas as
   linhas `info | Group ...` do devlog **não**. Isso precisa ser resolvido antes que qualquer diagnóstico por log seja
   confiável — possivelmente a thread pump não está agendando ou o `_dup2` não está capturando o `stdout` do fmt.
3. **`UseFullCoreWindowBounds()` está engolindo uma exceção** (`catch (...)`), o que explica os dois `_com_error` no
   log e significa que o `SetDesiredBoundsMode(UseCoreWindow)` **não está sendo aplicado** → o app roda na TV-safe area
   (relacionado ao T-13). Confirmar com o usuário se há bordas pretas.
4. **`WinRT: Using display resolution (gaming device unknown): 1920x1080`** num Xbox Series X — o esperado seria
   3840x2160. Reforça a hipótese de que o SDL3-uwp está entregando um tamanho errado de CoreWindow (T-13/T-12).

**Arquivos modificados nesta sessão (não commitados):**

| Arquivo | Mudança |
|---|---|
| `apps/ymir-sdl3/src/platform/uwp/uwp_platform.cpp` | tee do log para o debugger + diagnóstico do redirect |
| `apps/ymir-sdl3/src/platform/uwp/uwp_platform.hpp` | `RedirectStdioToLogFile()` agora devolve o caminho |
| `apps/ymir-sdl3/src/main.cpp` | marcadores de boot (temporário) |
| `apps/ymir-sdl3/src/app/app.cpp` | marcador de boot no `Run()` (temporário) |
| `apps/ymir-sdl3/src/app/services/gfx/gfx_context_impl_d3d12.cpp` | log detalhado da falha de swapchain |
| `libs/ymir-core/include/ymir/gpu/d3d12/d3d12_swap_chain.hpp` | `LastCreateHResult()` para diagnóstico |
| `AGENTS.md` (novo, na raiz) | instruções para agentes, incl. regras de deploy/commit/idioma |


### Entrada do agente Claude (máquina de origem, sem Xbox) — 2026-10-20

**Escopo lido:** a entrada do OpenCode acima, `AGENTS.md`, os commits `35e49d8c` e `d0616649`, e o fluxo inteiro de
`App::Run()` / `App::RunEmulator()`.

**Diagnóstico do "fica no splash e sai limpo":**

1. **A instrumentação perdia exatamente as linhas que explicam a saída.** `RedirectStdioToLogFile()` chamava
   `setvbuf(stdout, nullptr, _IOLBF, 4096)`, mas **o CRT da Microsoft não implementa line buffering: `_IOLBF` funciona
   como `_IOFBF` (buffer completo)**. As linhas do devlog ficavam presas no buffer de 4 KB, por isso não apareciam no
   Output (pendência 2 do OpenCode). Quando o app desistia cedo, o buffer só era descarregado no `exit`, dentro do
   pipe, e a thread pump *detached* era morta antes de drenar. Ou seja, a última mensagem, a que diz por que o app
   saiu, nunca chegava nem no arquivo nem no debugger.
2. **"Splash + saída limpa" = um `return` antecipado em `App::Run()`/`RunEmulator()`.** Todos os caminhos passam
   por `ShowStartupFailure(...)`:
   - `Failed to initialize SDL`;
   - `Failed to create window`;
   - **`Failed to initialize graphics.`** (D3D12 e os fallbacks falharam);
   - `Failed to create software framebuffer texture` / `display texture` / `logo texture`;
   - `Failed to create/start audio stream`;
   - ou `Could not create profile folders` (este com `return -1`).

   O `SDL_ShowSimpleMessageBox` desses caminhos provavelmente não aparece no Xbox, e é aí que entram os `_com_error`
   e o "island for this CoreWindow" do log.

**Mudanças (não commitadas; aguardando pedido do usuário, conforme AGENTS.md regra 10):**

| Arquivo | Mudança |
|---|---|
| `apps/ymir-sdl3/src/platform/uwp/uwp_platform.cpp` | stdout/stderr **sem buffer** (`_IONBF`); thread pump guardada (`g_logPumpThread`, ponteiro de propósito, para não chamar `std::terminate` em destrutor estático) e **joined** por `ShutdownLogRedirect()` |
| `apps/ymir-sdl3/src/platform/uwp/uwp_platform.hpp` | declara `ShutdownLogRedirect()` |
| `apps/ymir-sdl3/src/main.cpp` | guard RAII que chama `ShutdownLogRedirect()` em **qualquer** saída do `main()` |
| `apps/ymir-sdl3/src/app/app.cpp` | `ShowStartupFailure` também manda a mensagem via `OutputDebugStringA` (síncrono, independe do pipe) |

Compilado aqui: UWP Debug OK. Não foi executado, porque esta máquina não tem Xbox e o AGENTS.md proíbe o agente de
fazer deploy.

**Próximo passo (usuário / próximo agente):**

1. Rodar de novo por F5 no Xbox e copiar o Output. Procure **`[Ymir] Startup failure: ...`**: essa linha diz qual
   passo falhou. As linhas `info | ...` do devlog também devem aparecer agora, em ordem.
2. Se a falha for **`Failed to initialize graphics.`**, a mensagem lista o erro do D3D12 e o de cada fallback
   (SDLRenderer). Junto com a linha `Failed to create swapchain (CoreWindow): HRESULT ...` da instrumentação do
   OpenCode, isso decide entre swapchain, device ou tamanho.
3. Teste de bisseção sem recompilar: em `LocalState\StrikerX3\Ymir\Ymir.toml`, mude a seção `[Video]` para
   `GraphicsBackend = "SDLRenderer"`. Edite pelo Device Portal → File explorer, ou no PC no caminho da seção 1.2.
   - Se com SDLRenderer o app abre, o problema está isolado no contexto D3D12 do Ymir.
   - Se não abre, o problema é anterior ao gráfico, ou na janela/CoreWindow.

---

### Entrada do agente OpenCode #2 — 2026-10-20 (máquina de destino, COM Xbox, run real)

**Escopo lido:** as duas entradas acima (OpenCode #1 e Claude), o commit `d76a7eb1` inteiro, e o fluxo de
`App::Run()` a partir da criação do contexto gráfico.

**A correção do Claude funcionou e mudou o diagnóstico por completo.** Com `_IONBF` + `ShutdownLogRedirect()`, o log
agora sai inteiro no Output do VS. Isso **derrubou a hipótese da swapchain** e revelou o quadro real:

```
info  | App  | Direct3D 12 graphics context initialized successfully
info  | App  | Leaving full screen mode
```

**O D3D12 inicializa com sucesso. A swapchain não falha.** O T-12 e a suspeita de `DXGI_SCALING_NONE` /
`ALLOW_TEARING` deste documento estão **descartados** — nenhum log `Failed to create swapchain` apareceu, e a
instrumentação do `LastCreateHResult()` não foi disparada.

**O novo ponto exato de parada:** o app morre/para **depois de `Leaving full screen mode`** e **não** passa por
nenhum `ShowStartupFailure`. O próximo passo do código nesse ponto é `CreateTexture("[Ymir] Software framebuffer")`.
Não há log de erro nem mensagem de falha — ou seja, ou trava dentro da criação da textura (código do driver), ou para
em algum ponto posterior que ainda não tinha log.

**O que o log também mostra (achados novos):**

1. **O VDP está em software, não em D3D12:** `info | VDP-Config | Switched to Software VDP renderer`. Era esperado
   como fallback, mas vale confirmar que é o comportamento desejado no Xbox.
2. **O SDL3-uwp não está vendo o display do Xbox:**
   - `WinRT: Using display resolution (gaming device unknown): 1920x1080` — num Series X o esperado seria 3840x2160,
     e `gaming device unknown` sugere que a detecção de device falhou.
   - `info | App | Display 1 (Windows Simulator / Terminal Services Display) added` — o SDL está reportando um
     **display de Terminal Services / simulador**, não o display real do console.
   - `Window DPI scaling: 100.0%`.
   Isso é coerente com o `The island for this CoreWindow has not been initialized` e provavelmente é a causa raiz de
   qualquer problema de janela/CoreWindow (T-13 incluído).
3. **`gaming device unknown` + display errado** também explica o `_com_error` duplo: o caminho de
   `ApplicationView`/CoreWindow do SDL3-uwp falha e cai no fallback.

**Instrumentação adicionada nesta entrada (temporária, remover quando o boot estiver estável):**

`apps/ymir-sdl3/src/app/app.cpp` ganhou a macro local `YMIR_BOOT_TRACE(...)`, que emite via `OutputDebugStringA`
(síncrono, **não** depende do pipe nem do buffer do stdout), com 17 pontos cobrindo todo o trecho sem log entre
`Leaving full screen mode` e o loop principal:

- criação das texturas (software framebuffer, display, logo)
- init e start do áudio
- `Reset` do Saturn
- `StartEmulatorThread`
- `m_screenshotService.Start`
- `SDL_ShowWindow`
- `ReloadSDLGameControllerDatabases`
- entrada no `while (true)` principal

**Próximo passo (para o próximo agente):**

1. **Compilar com o VS fechado.** Nesta sessão o build falhou com `LNK1201: error writing to program database`
   porque o `devenv` (VS 2026) estava aberto segurando o `ymir-sdl3.pdb`. Fechar o VS antes de buildar.
2. **Rodar por F5 e procurar a última linha `[Ymir boot] ...`.** É ela que aponta o passo exato:
   - para em `creating software framebuffer texture` → o problema é a criação da textura (device/heap);
   - para em `initializing audio` / `starting audio stream` → é áudio;
   - para em `entering main loop` sem mais nada → o boot **completou** e o problema é render/present (voltar a olhar
     swapchain/present, agora com outra lente);
   - para em `showing window` → é o `SDL_ShowWindow` no CoreWindow.
3. **Investigar o display errado** (`Windows Simulator / Terminal Services Display` + `gaming device unknown`). Isso é
   independente do ponto de parada e provavelmente precisa de correção no SDL3-uwp (não no Ymir) — comparar com os
   projetos de referência `scummvm-uwp` e `x-files-uwp`, que lidam com o mesmo CoreWindow.

### Entrada do agente OpenCode #3 — 2026-10-20 (máquina de destino, COM Xbox, medição decisiva)

**Escopo lido:** as três entradas acima, o commit `d76a7eb1`, o loop principal de `App::Run()` e o caminho de
`Present()` do `Direct3D12GraphicsContext`.

**Dado crucial obtido com o usuário (muda o diagnóstico):**

> **O processo fica vivo.** A tela para no splash, mas o app **não** sai, **não** crasha e **não** mostra erro. Se o
> debugger for parado, ele ainda está rodando.

Isso **elimina** a hipótese de saída antecipada da entrada #2 (não é um `return` antecipado — não há
`ShowStartupFailure` algum). O app está **dentro do loop principal**, com a janela criada, apenas sem apresentar
frames novos.

**Também descartado:** o `gaming device unknown` / 1920x1080. O usuário confirmou que **o console está configurado em
1080p**. O `GetGamingDeviceModelInformation()` realmente falha, mas o resultado (1920x1080) coincide com o modo real
do console, então **isso é ruído e não a causa**. Anotado como item cosmético de baixa prioridade.

**Hipótese principal (medida, ainda não provada): `Present()` bloqueia para sempre.**

O caminho do frame é:

```
loop  →  m_graphicsService.Present()            (app.cpp:3449)
         →  Direct3D12GraphicsContext::Present()
            →  EndFrame(); ExecuteCommandLists()
            →  swapchain->Present(1, 0)          <- BLOQUEIA até um vblank que nunca chega
            →  MoveToNextFrame() → fenceFrame.Wait(INFINITE, ...)   <- 2o bloqueio possível
```

Evidência que sustenta:

1. **Se o `Present()` retornasse falha**, o loop cairia em `ShowFatalErrorDialog` + reset de backend + `goto end_loop`
   (app.cpp:3450-3459). Nada disso aparece no log. Logo o `Present()` **não retorna** — está bloqueado.
2. `swapchain->Present(1, 0)` (VSync) **bloqueia até a próxima apresentação vertical**. Em CoreWindow, a apresentação é
   dirigida pelo compositor; se o CoreWindow não estiver ativo/inicializado, **não há vblank** e a chamada não retorna.
3. O log tem exatamente o sintoma de CoreWindow não inicializado:
   `Msg:[The island for this CoreWindow has not been initialized.]` seguido de dois `_com_error` — vindos de
   `CoreWindow::GetForCurrentThread()` / `ApplicationView::GetForCurrentView()` no caminho de criação da janela do
   SDL3-uwp (`vendor/SDL3-uwp/src/video/winrt/SDL_winrtvideo.cpp:890` e `:892`).
4. O splash que fica na tela é coerente: é o último frame que o compositor tem; nada novo chega.

**Instrumentação adicionada nesta entrada (temporária, remover quando o boot estiver estável):**

`apps/ymir-sdl3/src/app/services/gfx/gfx_context_impl_d3d12.cpp` ganhou a macro `YMIR_PRESENT_TRACE(...)`
(`OutputDebugStringA`, síncrono) com marcas em:

- entrada do `Present()` + modo de apresentação usado;
- **logo depois de `swapchain->Present(...)`**, imprimindo o `HRESULT` — é esta linha que decide o diagnóstico;
- antes e depois do `fenceFrame.Wait(INFINITE, ...)` do `MoveToNextFrame()`;
- cada retorno do `Present()` (`Ok` / `Occluded` / `FAILED hr=...`).

Somadas aos 17 marcadores `[Ymir boot]` da entrada #2, o próximo run responde a pergunta sem ambiguidade.

**Leitura do próximo run:**

| Última linha no Output | Conclusão |
|---|---|
| `[Ymir boot] entering main loop` e depois `[Ymir present] Present() entered, mode=...` sem a linha `swapchain->Present returned` | **Confirmado: `Present(1, 0)` bloqueia no vblank.** Ir para "Correção" abaixo |
| `Present() entered` + `swapchain->Present returned hr=...` + `MoveToNextFrame: waiting on fence` sem `fence wait done` | Bloqueio na espera de fence (frame nunca completado pela GPU) |
| Várias linhas `Present() -> Ok` | O present funciona; o problema é outro (voltar para render/ImGui) |
| `Present() FAILED hr=...` | O `HRESULT` diz a causa direta |

**Correção a avaliar se o `Present(1, 0)` bloquear** (não aplicar antes de confirmar):

Em `gfx_context_impl_d3d12.cpp`, `Present()`, tornar o modo de publicação à prova de CoreWindow não inicializado.
As opções, da mais conservadora para a mais invasiva:

1. **Usar `Present(0, 0)` no UWP** (ou tratar o caso de CoreWindow como `NoSync`), deixando o Ymir controlar o ritmo
   pelo `screen.nextFrameTarget` que o loop já calcula (app.cpp:1530+). O loop **já** faz pacing próprio com sleep +
   spin até o deadline, então o VSync do swapchain é redundante e é justamente onde ele trava.
2. Logar `DXGI_PRESENT_ALLOW_TEARING`/`IsTearingSupported()` no Xbox e, se o flag estiver entrando numa swapchain de
   CoreWindow, removê-lo sob UWP.
3. Investigar no SDL3-uwp por que o `CoreWindow::GetForCurrentThread()` não está inicializado — é a causa raiz, mas
   fica no submódulo de terceiros (AGENTS.md regra 3), então só com justificativa forte.

**Pendências para o próximo agente:**

1. **Compilar e rodar; ler a tabela acima.** O build exige o **VS fechado** (LNK1201 no `.pdb` — aconteceu duas vezes).
2. Se confirmado o bloqueio do `Present(1, 0)`, implementar a opção 1 (a mais conservadora) sob `#if YMIR_PLATFORM_UWP`
   e validar no tile (não só no debugger — ver seção "Xbox startup" do AGENTS.md).
3. Registrar no `docs/uwp/` o resultado, incluindo se a opção 1 resolve — e documentar o caminho que **não** funcionou,
   conforme a regra de documentação do `AGENTS.md`.

### Entrada do agente OpenCode #4 — 2026-10-20 (máquina de destino, COM Xbox, correção do diagnóstico)

**Escopo lido:** todas as entradas acima, o run 04 registrado em `docs/uwp/evidence/`, o caminho de
`App::Run()` a partir da criação do contexto gráfico, e `AudioSystem::Init`.

**A hipótese da entrada #3 estava errada, e o run prova.** Com 17 marcadores cobrindo todo o caminho entre o contexto
gráfico e o loop, a parada é bem antes do `Present()`:

```
[Ymir boot] creating software framebuffer texture
info  | App | Direct3D 12 graphics context initialized successfully
info  | App | Leaving full screen mode
[Ymir boot] software framebuffer texture OK
[Ymir boot] creating display texture
[Ymir boot] display texture OK
[Ymir boot] display texture OK, stbi logo load next
[Ymir boot] initializing audio
'ymir-sdl3.exe' (Win32): Loaded 'C:\Windows\System32\AudioSes.dll'. ...     <-- nada depois
```

**O loop principal nunca é alcançado.** Nenhuma linha `[Ymir present]` apareceu — as 9 marcas do caminho de
apresentação, adicionadas especificamente para testar a hipótese do vblank, não produziram saída alguma. Aquela
hipótese está **morta** e não deve ser retomada.

**Novo ponto de parada, medido:** `AudioSystem::Init()`, que chama
`SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, ...)`. O marcador seguinte do boot
(`starting audio stream`, antes de `AudioSystem::Start()`) **nunca aparece**, então a parada é dentro do `Init`.
`AudioSes.dll` (WASAPI) é carregada exatamente nesse momento, o que é coerente com uma enumeração/abertura do endpoint
de reprodução padrão que não retorna.

**Ressalva honesta:** o carregamento da `AudioSes.dll` mostra que o caminho do WASAPI foi tocado, mas **não prova** que
`SDL_OpenAudioDeviceStream` não retorna. Para eliminar a dúvida, `apps/ymir-sdl3/src/app/audio_system.cpp` foi
instrumentado com marcas `[Ymir audio]` síncronas (`OutputDebugStringA`):

- antes e depois de `SDL_OpenAudioDeviceStream`, incluindo o valor de retorno e o `SDL_GetError()` em caso de falha;
- em `Start()`, antes e depois de `SDL_ResumeAudioStreamDevice`.

O run 05, com esse build, responde sem ambiguidade. **Build compilado com sucesso (Debug UWP, MSIX gerado) nesta
entrada.**

**Evidências organizadas para os próximos agentes.** Criado `docs/uwp/evidence/`, com:

- `README.md` — o problema, o índice dos runs, a leitura do run mais recente, as hipóteses ranqueadas, as hipóteses já
  **eliminadas** com o motivo, e os fatos de ambiente para reproduzir;
- `run-2026-10-20-03-texture-markers.raw.txt` — log bruto do run que provou que o D3D12 inicializa;
- `run-2026-10-20-04-audio-hang.raw.txt` — log bruto do run que localizou a parada no áudio.

A partir de agora, **cada run do Xbox deve ter o log bruto guardado nessa pasta** (`run-<data>-<nn>-<resumo>.raw.txt`) e
o `README.md` atualizado, para que o diagnóstico não dependa de resumo de conversa.

**Hipóteses ainda vivas, em ordem:**

1. `SDL_OpenAudioDeviceStream` bloqueia abrindo o dispositivo de reprodução padrão no Xbox/UWP. **Mais provável**, e é o
   que o run 05 decide.
2. A parada é no driver/SO de áudio *depois* de a stream abrir. Menos provável: `starting audio stream` teria aparecido.
3. Estado corrompido antes, fazendo o áudio travar — por exemplo pelos dois `_com_error` de
   `CoreWindow::GetForCurrentThread()` / `ApplicationView::GetForCurrentView()`, ligados ao sintoma
   `The island for this CoreWindow has not been initialized` que aparece em **todos** os runs. Especulativo.

**Direções de correção a avaliar se for o item 1** (não aplicar antes de confirmar com o run 05):

- **Deixar o áudio falhar graciosamente.** `AudioSystem::Init` já retorna `false` quando a stream não abre; verificar se
  o `App::Run()` trata esse `false` sem bloquear — se não tratar, esse é o bug de robustez, independentemente da causa.
- Inicializar o subsistema de áudio do SDL em **thread separada com timeout**, para que um endpoint problemático não
  congele o boot.
- Comparar com os projetos de referência `scummvm-uwp` e `x-files-uwp`, que abrem áudio no Xbox e podem já ter
  resolvido isso (querystring de dispositivo, formato, ou uso de um backend específico).

**Pendências para o próximo agente:**

1. Rodar o build atual (run 05) e ler as marcas `[Ymir audio]`. Registrar o log bruto em `docs/uwp/evidence/`.
2. Conforme o resultado, aplicar a correção correspondente e **validar pelo tile**, não só pelo debugger.
3. Manter o `docs/uwp/evidence/README.md` atualizado a cada run.

**Nota sobre a instrumentação temporária:** as macros `YMIR_BOOT_TRACE` (app.cpp/main.cpp), `YMIR_PRESENT_TRACE`
(gfx_context_impl_d3d12.cpp) e `YMIR_AUDIO_TRACE` (audio_system.cpp) são **temporárias** e devem ser removidas quando o
boot estiver estável. Todas aparecem só sob `YMIR_PLATFORM_UWP`.


### Entrada do agente Claude — 2026-10-20 (2ª): deadlock na abertura do áudio

**Escopo lido:** commit `def6f691`, `docs/uwp/evidence/README.md`, raw do run 04 e o caminho WASAPI do SDL3-uwp
(`vendor/SDL3-uwp/src/audio/wasapi/SDL_wasapi.c` e `SDL_wasapi_winrt.cpp`).

**Causa provável (lida no código, falta o run 05 confirmar):**
- o `SDL_OpenAudioDeviceStream()` na **UI thread** repassa a ativação pra *management thread* do SDL e **bloqueia a
  UI thread**;
- a management thread chama `ActivateAudioInterfaceAsync()` e **bloqueia** esperando o callback;
- no UWP essa ativação é atendida pela UI thread, que está parada.

Resultado: **deadlock**. Isso bate com o run 04: processo vivo, `AudioSes.dll` como último módulo carregado, nenhum
marcador depois de `initializing audio`. Detalhes em `docs/uwp/evidence/README.md` → "Root cause analysis of run 04".

**Correção (não commitada, aguardando pedido do usuário):** `apps/ymir-sdl3/src/app/audio_system.cpp`, só no UWP:
- abre o áudio numa thread auxiliar enquanto a UI thread chama `SDL_PumpEvents()` em loop;
- com timeout de 10 s, depois do qual falha com mensagem clara em vez de travar;
- o SDL3-uwp não foi alterado.

Compilado aqui: UWP Debug e desktop.

**Próximo passo:** run 05 no Xbox. Esperado no Output:
1. `[Ymir audio] Init(): SDL_OpenAudioDeviceStream returned a stream`;
2. `Start(): OK`;
3. `Audio stream opened: ...`;
4. a UI do Ymir na tela.

Se aparecer `Timed out after 10 seconds opening the default audio playback device`, a hipótese do deadlock está
errada; siga o README de evidências.


### Entrada do agente OpenCode #5 — 2026-10-20 (máquina de destino, COM Xbox, causa raiz confirmada)

**Run 05 confirmou o bloqueio, sem ambiguidade.** O build com as marcas `[Ymir audio]` produziu:

```
[Ymir boot] initializing audio
[Ymir audio] Init(): opening default playback device (freq=44100 format=8010 channels=2)
'ymir-sdl3.exe' (Win32): Loaded 'C:\Windows\System32\AudioSes.dll'. ...
```

A marca **antes** da chamada saiu; a marca **depois** (`Init(): SDL_OpenAudioDeviceStream returned ...`) não.
**`SDL_OpenAudioDeviceStream` não retorna.** Formato pedido: S16LE, 44100 Hz, estéreo — nada exótico, então não são os
parâmetros.

**Causa raiz identificada no código do SDL3-uwp.** Cadeia:

```
SDL_OpenAudioDeviceStream → SDL_OpenAudioDevice → WASAPI_OpenDevice → ActivateWasapiDevice
  → WASAPI_ActivateDevice                      (src/audio/wasapi/SDL_wasapi_winrt.cpp:292)
     → ActivateAudioInterfaceAsync(...)        (mesmo arquivo:306)   <-- assíncrono, servido pela ASTA do app
     → handler->WaitForCompletion()            (mesmo arquivo:317)   <-- SDL_WaitSemaphore, SEM TIMEOUT
```

O próprio SDL documenta o risco no local, em `SDL_wasapi_winrt.cpp:316`:

> `// !!! FIXME: the problems in SDL2 that needed this to be synchronous are _probably_ solved by SDL3, and this can`
> `// block indefinitely if a user prompt is shown to get permission to use a microphone.`

`ActivateAudioInterfaceAsync` é assíncrono e sua conclusão é entregue pelo **dispatcher do CoreWindow** — que só é
bombeado pela thread principal (a ASTA). Como essa thread é justamente a que está bloqueada dentro de
`SDL_OpenAudioDeviceStream`, a ativação nunca completa e o semáforo nunca é liberado. Deadlock.

Por que `SDL_Init(SDL_INIT_AUDIO)` (app.cpp:639) **passa** e a abertura do dispositivo **não**: o `Init` só faz a
*enumeração* de endpoints, cujos eventos do watcher chegam por outra via. A *ativação* do endpoint só acontece na
abertura — e é ali que trava.

**Confirmação do mecanismo de desbloqueio.** O `SDL_PumpEvents()` da thread principal chega em:

```
SDL_PumpEvents → SDL_PumpEventsInternal (src/events/SDL_events.c:1501, `SDL_assert(SDL_IsMainThread())`)
  → WINRT_PumpEvents (src/video/winrt/SDL_winrtevents.cpp:51)
     → SDL_WinRTApp::PumpEvents (src/core/winrt/SDL_winrtapp_direct3d.cpp:418)
        → CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent)
```

A linha `SDL_winrtapp_direct3d.cpp:426` **é** o bombeamento do dispatcher da ASTA. Portanto a abordagem do agente
Claude — abrir o dispositivo numa worker thread enquanto a thread principal bombeia — ataca a causa correta.

**Riscos conhecidos da abordagem (aceitos, documentados):**

1. Em `SDL_winrtapp_direct3d.cpp:669-672` o próprio SDL avisa que o `ProcessEvents` **não retorna** após entregar um
   evento de *suspend*; e a variante `ProcessOneAndAllPending` (`:435`) bloqueia quando não há eventos pendentes. Se o
   app for para background durante a janela de boot, o `SDL_PumpEvents()` pode travar em vez de retornar, e o timeout
   de 10 s nunca dispara.
2. O `opener.detach()` no timeout deixa uma thread viva dentro do SDL; se o app prosseguir e chamar `SDL_Quit()`, há
   risco de uso concorrente das estruturas do SDL no teardown.
3. A causa raiz permanece no `vendor/SDL3-uwp`, que a `AGENTS.md` regra 3 e o `AGENTS.md` do próprio SDL proíbem
   alterar (contribuições de IA são vedadas lá). O contorno fica no Ymir, o que é o local correto dadas as restrições.

**Rota alternativa levantada (não tentada ainda, plano B).** Existe um projeto no workspace do usuário,
`F:\workspace\ymir-xbox-with-real-audio`, que **já roda áudio real no Xbox** — não via SDL/WASAPI, mas via
**XAudio2** (`src/audio/XAudio2Output.cpp`): `XAudio2Create` → `CreateMasteringVoice` → `CreateSourceVoice`, tudo
síncrono, sem `ActivateAudioInterfaceAsync`, sem semáforo, sem dependência de message pump. É a API de áudio nativa do
Xbox e não tem esse modo de falha.

Verificado nesta máquina: `xaudio2.lib` e `xaudio2.h` estão presentes no SDK 10.0.28000.0, que é exatamente o
`WindowsTargetPlatformVersion` usado pelo build UWP (`out/build/UWP-x86-64-AVX2-MSVC/apps/ymir-sdl3/ymir-sdl3.vcxproj`),
tanto em `um/x64` quanto em `Include/um`. Ou seja, a rota é viável sem dependência externa nova.

Se a abordagem da worker thread falhar, **esta é a rota a seguir**: implementar um backend de áudio XAudio2 no Ymir,
atrás de uma interface, selecionado sob `YMIR_PLATFORM_UWP` — em vez de insistir em contornar o WASAPI do SDL.

**Instrumentação adicionada nesta entrada:** `OpenAudioDeviceStreamWithoutBlockingUIThread` ganhou marcas
`[Ymir audio] open-on-worker:` — início da worker, retorno da worker, número de iterações de pump e detecção de
timeout. O run 06 com esse build distingue as três saídas possíveis: worker retornou stream, worker retornou nullptr,
ou timeout com a worker presa.

**Pendências para o próximo agente:**

1. Rodar o build atual (run 06) e ler as marcas `open-on-worker:`. Registrar o log bruto em `docs/uwp/evidence/`.
2. Se destravar: confirmar que o app passa do splash e chega ao loop principal, e **validar pelo tile**, não só pelo
   debugger.
3. Se ainda travar: ir para a rota XAudio2 descrita acima.



### Entrada do agente OpenCode #6 — 2026-10-20 (máquina de destino, COM Xbox, backend XAudio2)

**Run 06 derrubou a hipótese da worker thread.** O build com as marcas `open-on-worker:` produziu:

```
[Ymir audio] open-on-worker: starting worker thread
[Ymir audio] open-on-worker: worker calling SDL_OpenAudioDeviceStream
'ymir-sdl3.exe' (Win32): Loaded 'C:\Windows\System32\AudioSes.dll'. ...
[Ymir audio] open-on-worker: wait loop ended after 4994 pump iterations, done=false
[Ymir audio] open-on-worker: TIMED OUT, detaching the worker thread
[Ymir audio] Init(): SDL_OpenAudioDeviceStream returned nullptr
[Ymir audio] Init(): FAILED, SDL error: Timed out after 10 seconds opening the default audio playback device
[Ymir] Startup failure: Failed to create audio stream: Timed out after 10 seconds opening the default audio playback device
```

Cerca de **5000 chamadas de `SDL_PumpEvents()` em 10 segundos não destravaram a ativação**. A cadeia do pump
(`SDL_PumpEvents` → `WINRT_PumpEvents` → `SDL_WinRTApp::PumpEvents` →
`CoreWindow::Dispatcher->ProcessEvents(ProcessAllIfPresent)`) estava correta — o mecanismo existe e é alcançado —
mas bombear o dispatcher do CoreWindow **não** completa a ativação assíncrona do endpoint de áudio. A suposição
compartilhada por ambos os agentes, de que a ASTA seria o serviço daquele callback, é **falsa**.

Efeitos colaterais confirmados: o timeout produziu uma falha limpa (comportamento correto), a linha
`[Ymir] Startup failure: ...` apareceu no Output (o espelhamento do log está 100% funcional) e as threads
`VDP1/VDP2/deinterlace render thread` saíram enquanto a worker do áudio **detachada** seguia viva dentro do SDL —
exatamente o risco registrado na entrada #5.

**Decisão: parar de contornar o WASAPI do SDL.** A causa é estrutural (o SDL usa
`ActivateAudioInterfaceAsync` + espera sem timeout no caminho UWP/WinRT, e o `vendor/SDL3-uwp` não pode ser
alterado), então o contorno no lado do Ymir tem que trocar a API de áudio, não o timing da chamada.

**Implementado: backend de áudio XAudio2 para UWP/Xbox.** XAudio2 é a API de áudio nativa do Xbox e é totalmente
síncrona — `XAudio2Create`, `CreateMasteringVoice` e `CreateSourceVoice` retornam inline. Não há
`ActivateAudioInterfaceAsync`, não há semáforo, não há dependência de message pump, portanto não existe o modo de
falha observado.

Arquitetura (SOLID; nenhum `#if` fora de um único ponto de escolha):

| Arquivo | Papel |
| --- | --- |
| `src/app/audio_backend.hpp` | Interface `AudioBackend`, **pull**: o dispositivo pede frames; `Sample` também vive aqui |
| `src/app/sdl_audio_backend.{hpp,cpp}` | Backend de desktop, via stream SDL |
| `src/app/xaudio2_backend.{hpp,cpp}` | Backend UWP/Xbox, via XAudio2 (pool de 4 buffers, callback de reciclagem) |
| `src/app/audio_system.cpp` | Escolhe o backend (único `#if`), mantém ring buffer, ganho, mute, sync/silent |

O `AudioSystem` manteve a API pública inteira e agora só delega o I/O. `Start`/`Stop`/`ReceiveSample`/
`GetAudioStreamFormat` mantêm a semântica anterior, e `GetAudioStreamFormat` passou a devolver o formato
configurado (S16LE na taxa pedida) em vez de consultar o dispositivo, porque os dois backends o garantem.

**O contorno anterior foi removido.** `OpenAudioDeviceStreamWithoutBlockingUIThread` e a worker thread com
`detach()` não existem mais — inclusive isso elimina o risco da thread órfã durante o teardown observado no run 06.

**Verificação de viabilidade (feita antes de escrever o backend, não presumida):** `xaudio2.h` e `xaudio2.lib`
estão presentes em `10.0.28000.0`, que é o `WindowsTargetPlatformVersion` deste build. O `xaudio2.lib` importa
diretamente de `XAudio2_9.dll` (presente em `System32`, 602 KB), sem camada de apiset. O target `ymir-sdl3` passou a
linkar `xaudio2` sob `if (Ymir_PLATFORM_UWP)`. **Build Debug compila e linka, MSIX gerado.**

Armadilha do SDK encontrada no caminho: nesta versão o `IXAudio2::CreateMasteringVoice` tem **7 parâmetros**
(`ppMasteringVoice, InputChannels, InputSampleRate, Flags, DeviceId, pEffectChain, StreamCategory`) — não existe o
parâmetro `ChannelMask` que aparece em exemplos mais antigos. O `AudioCategory_GameEffects` é o último argumento.

**Instrumentação adicionada:** o backend emite por `SDL_LogInfo`/`SDL_LogError`
`[Ymir audio] XAudio2 engine created`, `XAudio2 mastering voice created (N ch, M Hz)`, `XAudio2 source voice created`,
`XAudio2 playback started`, e o erro específico de cada etapa em caso de falha. O run 07 com este build indica
exatamente em qual das três criações o XAudio2 pararia, se parar.

**Pendências para o próximo agente:**

1. Rodar o build atual (run 07) no Xbox e registrar o log bruto em `docs/uwp/evidence/`. O esperado é o app passar
   do splash e entrar no loop principal.
2. **Validar pelo tile, não só pelo debugger** — o comportamento de ativação e o teto de memória/GPU diferem.
3. Se o XAudio2 funcionar: o áudio volta a ser real (não mutado) e o `AudioSystem` mantém o comportamento de sync.
4. Se o XAudio2 também falhar: checar em qual das três criações parou (as marcas dizem), e considerar
   `XAudio2CreateWithSharedContexts` ou revisar a categoria de stream.


### Claude agent entry — 2026-10-21 (source machine, no Xbox): run 07, splash → black screen

> Written in English per `AGENTS.md` rule 9. This is the complete record of the session: what was observed, how it was
> reasoned about, what every other Xbox port was checked for, what changed (file by file), how it was verified, what
> was committed and pushed, and exactly what to do next. The raw analysis lives in `docs/uwp/evidence/README.md` →
> "Run 07" and "Cross-check against other Xbox ports"; the decisions are D17–D19 in `README.md` §4.

#### 1. Starting point

- Branch `uwp/main` at `071639c9` ("Replace the blocked SDL audio path with an XAudio2 backend"), working tree clean.
- The source machine has **VS Build Tools 2026** (`C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`)
  but no Visual Studio IDE and no Xbox, so nothing could be run here (`AGENTS.md` rule 8 forbids it anyway).
- **Run 07** (reported by the user, no log kept): the first run with the XAudio2 backend, **F5 with a Debug build**.
  The splash screen went away, then the screen turned **black** and stayed black; the process stayed alive.

#### 2. What run 07 proves (read from the code, not guessed)

1. SDL3-uwp only calls `CoreWindow::Activate()`, which is what dismisses the splash, from
   `SDL_WinRTApp::OnAppActivated` (`vendor/SDL3-uwp/src/core/winrt/SDL_winrtapp_direct3d.cpp:613-615`).
2. That handler is dispatched by the CoreWindow dispatcher, which is only pumped by `SDL_PumpEvents()`
   (`SDL_winrtapp_direct3d.cpp:426`). The WinRT video driver has no ShowWindow hook, so `SDL_ShowWindow()` does not
   pump; neither do the D3D12 init, XAudio2 or any other step of the boot.
3. Therefore the first pump is the first `SDL_PollEvent()` of the main loop (`apps/ymir-sdl3/src/app/app.cpp`,
   "Process SDL events"), and **run 07 reached the main loop: the XAudio2 backend works.** This also explains why the
   splash stayed up during the audio deadlock of runs 04–06: the window was never activated.
4. Without a BIOS the Saturn output is black, so the only visible things should be the ImGui menu bar and the
   *Welcome* modal (`app.cpp`, `OpenWelcomeModal(true)` when no IPL ROM is found). A black screen means the UI thread
   stopped in the first frames, or frames are presented with no UI in them.

#### 3. Hypotheses, as ranked after reading Ymir and before looking at other ports

| # | Hypothesis | Mechanism in Ymir |
|---|---|---|
| H1 | Visual Studio is in break mode (Debug build) | `gfx_context_impl_d3d12.cpp` called `DebugLayer::BreakOnWarnings(device, true)` → `SetBreakOnSeverity` for WARNING, ERROR and CORRUPTION (`libs/ymir-core/src/ymir/gpu/d3d12/d3d12_debug.cpp:85-106`); run 06 loads `d3d12SDKLayers.dll`. Or an `IM_ASSERT` in `ImGui_ImplDX12_NewFrame`, which creates the ImGui pipeline lazily on the first frame (`vendor/imgui/imgui/backends/imgui_impl_dx12.cpp:1072-1074`); `ymir_imconfig.h` does not redefine `IM_ASSERT` |
| H2 | `Present(1, 0)` or the frame fence wait blocks | Entry #3's hypothesis. It was "eliminated" after run 04 only because the loop was never reached — **never actually tested**, so reopened |
| H3 | Frames are presented but ImGui draws nothing | Silent pipeline failure in Release; `ImGuiInit()`'s result was ignored (`app.cpp`); or `io.DisplaySize` = 0 when SDL flags the window MINIMIZED (`imgui_impl_sdl3.cpp:979`), which only happens after `VisibilityChanged(false)` |
| H4 | The UI thread blocks in `SDL_PollEvent()` | `PumpEvents` switches to `ProcessEvents(ProcessOneAndAllPending)`, which waits for an event, when SDL thinks the window is invisible and no hide event is pending (`SDL_winrtapp_direct3d.cpp:390-436`) |
| H5 | Activation deadline when launched from the tile | Activation only happens after the whole boot. Not applicable under F5 (the debugger disables it), and it kills rather than freezes |

Ruled out by reading the code: the video-sync wait on `frameReadyEvent` (`syncInWindowedMode` defaults to `false`,
`settings.cpp:1123`, and the log says `Leaving full screen mode`), and audio sync (it can only stall the emulator
thread, never the UI thread).

#### 4. Diagnostic gaps found along the way

- `YMIR_BOOT_TRACE`/`YMIR_PRESENT_TRACE` only called `OutputDebugStringA`, which goes nowhere without a debugger: a run
  launched from the tile left no trace in `ymir-uwp.log`.
- `YMIR_PRESENT_TRACE` printed 3+ lines per frame (180+ per second), flooding the Output window.
- The XAudio2 backend logged through `SDL_LogInfo(SDL_LOG_CATEGORY_AUDIO, ...)`, but SDL filters every category except
  APPLICATION at ERROR priority by default (`vendor/SDL3-uwp/src/SDL_log.c:397-410`), so its lines never printed.
- `m_graphicsService.ImGuiInit()`'s return value was ignored.

#### 5. Cross-check against other Xbox ports (the user asked for it)

Source reading only, nothing built or run. The user's own ports and the projects already in
`D:\workspace\_non_work_\` were read in place. Four more were **shallow-cloned** there for this purpose
(`git clone --depth 1 --single-branch`, no submodules, read only):

- `xemu-uwp` (branch `uwp-d3d12`, DanP142);
- `Cemu-UWP` (branch `uwp-d3d12`, DanP142);
- `Azahar-UWP` (branch `UWP`, DanP142);
- `flycast` (upstream `master`).

They came from the Emulation Revival list (`https://emulationrevival.github.io/xbox-dev-mode/emulators.html`).

| Project | What it contributed |
|---|---|
| scummvm-uwp (user) | `docs/XBOX-TILE-LAUNCH-KILL.md`: three mechanisms the debugger switches off (ODS on a coroutine stack, late activation, 1 GB App cap); fix = early `Activate`, async boot, ODS guard. Commit `226d29f`: a real "debug layer breakpoint on Xbox". Repeated black screens from two swap chains on one CoreWindow, or from never presenting |
| dosbox-pure-uwp (user) | Activates in `OnActivated`, defers heavy loading; `Present1(1, 0)` works; XAudio2 for audio |
| sm64ex-uwp | `Present(1, 0)` + waits on the waitable object (1000 ms timeout); runtime `D3DCompile`, ships `d3dcompiler_47.dll` |
| XBSX2 | **Enhanced barriers forced off on Xbox** ("Not supported on Xbox", `pcsx2/GS/Renderers/DX12/GSDevice12.cpp`); no waitable object; scaling left at STRETCH; clears and presents right after creating the swap chain; debug break only when opted in **and** `IsDebuggerPresent()`; deny-lists `INCOMPATIBLE_BARRIER_LAYOUT` ("The current OS version of D3D12 (616) has a validation bug"); Series S swap chain capped at 1440p ("if you try to make a 4K Swap Chain the application will crash"); runtime `D3DCompile` for ImGui |
| RetroArch | `Present(swap_interval, 0)` on the UI thread on Xbox; waitable swap chains **off by default on WinRT** (`config.def.h`); runtime `D3DCompile` with exactly vs_5_0/ps_5_0; debug break on ERROR only |
| SDL3-uwp's own D3D12 renderer | On WinRT: **no waitable flag, no `SetMaximumFrameLatency`, no tearing** (commit `7a7e54b5d`); same present-then-fence-wait pattern as Ymir; breaks on ERROR/CORRUPTION only |
| xenia (Blades-UWP) | STRETCH forced "to workaround the zoom bug"; tearing default off on UWP; no waitable; debug/breaks all opt-in; precompiled ImGui shaders |
| Dolphin | STRETCH; runtime `D3DCompile` vs_5_0/ps_5_0 for ImGui; validation opt-in |
| PPSSPP | Swap chain created before activation works; "In UWP, the CoreWindow is always 1920x1080, even when running at 4K" |
| xemu-uwp | Closest to Ymir (SDL3 + `imgui_impl_dx12` + D3D12): checks `ImGui_ImplDX12_Init`/`CreateDeviceObjects`; STRETCH; waits on the waitable object with a 100 ms bound; presents black on all buffers right after creation; debug layer opt-in, only logged |
| Cemu-UWP | STRETCH, flags 0; patched ImGui DX12 backend using DXC; fence waits bounded; debug layer only logged |
| flycast | DX11, STRETCH, tearing only after a feature check; `imgui_impl_dx11` with runtime `D3DCompile` (system DLL) |
| Azahar-UWP | OpenGL through Mesa (no DXGI code); late Activate via SDL2 works |

**Verdicts after the cross-check:**

- **H1 is the best-supported hypothesis.** No Xbox port breaks on D3D12 *warnings* by default.
- **H2 is weakened.** `Present(1, 0)` works on Xbox in RetroArch, XBSX2, xemu, Cemu and SDL3-uwp.
- **H3:**
  - `D3DCompile` itself is ruled out; it works at runtime on Xbox in RetroArch, Dolphin, XBSX2, flycast and xemu.
  - An init or pipeline failure is still possible, and is now checked.
- **H4:** the mechanism is confirmed in SDL3-uwp, but no port has observed it.
- **H5:** not applicable to F5; it matters for tile launches.

**Differences between Ymir and every working port:**

1. Enhanced barriers on every frame.
2. A waitable swap chain that is never waited on, with `SetMaximumFrameLatency(3)`.
3. Tearing on a CoreWindow swap chain.
4. `DXGI_SCALING_NONE`.
5. Break on WARNING.

#### 6. Changes — commit `dc7d015e` "uwp: Make the next Xbox run locate the black screen after the splash"

All under `YMIR_PLATFORM_UWP` or in port-only files.

| File | Change |
|---|---|
| `apps/ymir-sdl3/src/platform/uwp/uwp_platform.{hpp,cpp}` | New `platform::uwp::Trace(std::string_view)`: appends a newline if missing, calls `OutputDebugStringA`, then `fwrite`+`fflush` straight into the log `FILE*` (`g_logFile`). It is synchronous, so a line survives a crash right after the call. The CRT locks the stream per `fwrite`, so lines never interleave with the pump thread's. Before the redirect and after `ShutdownLogRedirect()` it only reaches the debugger |
| `apps/ymir-sdl3/src/platform/uwp/stall_watchdog.{hpp,cpp}` (new) | `platform::uwp::StallWatchdog`. `Mark(phase)` (lock-free: relaxed store of the phase pointer + release increment of a sequence number); `SetFrame(n)`. A monitor thread polls every 500 ms; after the threshold (3 s) without a new mark it logs `[Ymir watchdog] UI thread stalled for N ms in phase '...' (frame N)`, repeats every 5 s, and logs `resumed after N ms` when progress comes back. No global state: it is a local object in `App::RunEmulator` |
| `apps/ymir-sdl3/CMakeLists.txt` | Adds the watchdog sources to the `if (Ymir_PLATFORM_UWP)` block |
| `apps/ymir-sdl3/src/app/app.cpp` | `YMIR_BOOT_TRACE` goes through `Trace()`. `ShowStartupFailure` uses `Trace()`. `App::Run()` entry marker uses the macro. **`ImGuiInit()` is checked**: on failure, `ShowStartupFailure("Failed to initialize the Dear ImGui backends ...")` and return. The watchdog starts right before `SDL_ShowWindow` ("UI", 3 s). `YMIR_UI_PHASE(...)` marks: show window, reload controller databases, frame pacing, poll SDL events, wait for emulator frame and update framebuffer texture, ImGui new frame, build UI, ImGui render, draw Saturn screen, ImGui draw, present, end of frame. Frame counter `uwpFrameCount`. **Geometry trace** after `ImGui::NewFrame()`: window size (DIPs and pixels), flags (hidden/minimized/occluded/input focus), `io.DisplaySize`, `io.DisplayFramebufferScale`, on the first frame and on every change |
| `apps/ymir-sdl3/src/app/services/gfx/gfx_context_impl_d3d12.cpp` | `YMIR_PRESENT_TRACE` goes through `Trace()`, gated by `Impl::tracePresent`: the first 10 frames (`kVerbosePresentFrames`) in full, then one frame every 5 s (`kPresentTraceInterval`), with the frame number. **`BreakOnWarnings(device, false)` on UWP**: messages are still printed. **`ImGui_ImplDX12_CreateDeviceObjects()` right after `ImGui_ImplDX12_Init`**; on failure both backends are shut down, `m_imguiInitialized = false`, and an error is logged; `Dear ImGui backends initialized: yes/no` is always logged |
| `apps/ymir-sdl3/src/main.cpp` | The post-redirect boot markers go through `Trace()` so they reach the log file |
| `apps/ymir-sdl3/src/app/xaudio2_backend.cpp` | `SDL_LogInfo`/`SDL_LogError` → `Trace()` (engine, mastering voice, source voice, playback started, and the three failures) |
| `docs/uwp/evidence/README.md` | Run 07 section, hypotheses, diagnostic gaps, instrumentation table, "How to read run 08" table; the Present hypothesis marked **REOPENED** |
| `docs/uwp/HANDOFF.md`, `docs/uwp/README.md` | This entry; §1/§5 rows; watchdog in the delta map |

#### 7. Changes — commit `52d0abf7` "uwp: Align the D3D12 swap chain and barriers with ports that work on Xbox"

Applied at the user's request ("align everything now") instead of waiting for run 08.

| File | Change | Why (decision) |
|---|---|---|
| `apps/ymir-sdl3/src/app/services/gfx/gfx_context_impl_d3d12.cpp` | `features.enhancedBarriers = false` on UWP after the `OPTIONS12` check; the reported value is traced: `[Ymir gfx] D3D12 enhanced barriers reported: ...; forced off on UWP` | D17, XBSX2 |
| same file | Swap chain: `DXGI_SCALING_STRETCH` and `Flags = 0` on UWP (desktop keeps `SCALING_NONE` + `FRAME_LATENCY_WAITABLE_OBJECT`) | D18, SDL3-uwp / RetroArch / xenia / Dolphin / XBSX2 / Cemu |
| `libs/ymir-core/include/ymir/gpu/d3d12/d3d12_swap_chain.hpp` | On UWP, CoreWindow swap chains: tearing forced off; `SetMaximumFrameLatency`/`GetFrameLatencyWaitableObject` skipped when the waitable flag is absent (`m_usesWaitableObject`); `IsValidExt()` no longer requires a waitable object in that case (it would otherwise report the swap chain as invalid) | D18 |
| `libs/ymir-core/src/ymir/hw/vdp/renderer/vdp_renderer_hw_d3d12.cpp` | Enhanced barriers forced off on UWP before `barrierTracker.UseEnhancedBarriers(...)` | D17 |
| `docs/uwp/README.md` | Decisions D17 (enhanced barriers), D18 (swap chain), D19 (no debug break) in §4; delta map entries | — |
| `docs/uwp/evidence/README.md` | "Cross-check against other Xbox ports" | — |

**Accepted trade-off:**

- If run 08 works, it will not say which of the changes fixed it.
- To bisect, revert them one at a time, in this order: debug break, enhanced barriers, waitable/latency, tearing,
  STRETCH.
- The enhanced-barrier trace and the still-printed debug-layer messages narrow it down without a rebuild.

#### 8. Verification done on the source machine

| Check | Result |
|---|---|
| `pwsh scripts/uwp/build.ps1 -Configuration Debug` | OK, MSIX generated (`...\ymir-sdl3_0.4.2.0_x64_Debug_Test\`) |
| `pwsh scripts/uwp/build.ps1 -Configuration Release` | OK, MSIX generated (`...\ymir-sdl3_0.4.2.0_x64_RelWithDebInfo_Test\`) |
| Desktop `cmake --build out\build\Full-x86-64-AVX2-MSVC-Release --target ymir-sdl3` (from a `vcvars64.bat` environment) | OK |
| Desktop `ctest` | **14/14** passed |
| New compiler warnings in changed lines | none (the C4244/C4267 warnings in the log are from pre-existing lines) |
| `clang-format` | applied to the new files; the other diffs only touch pre-existing lines, left alone (rule 1) |
| Run on the Xbox | **not done** — the user runs it |

#### 9. Git

- Commits `dc7d015e` and `52d0abf7` on `uwp/main`, pushed to `origin` (`071639c9..52d0abf7`). The commit identity is
  the personal one (`marcelofrau@gmail.com`).
- The first commit was split out of the working tree by temporarily removing the alignment hunks. It matches the
  instrumentation-only state that was built earlier in the session.
- A stale `.git/index.lock` (0 bytes) blocked the first commit. No `git.exe` was running, so only that file was
  removed, per T-18.
- `.claude/plans/` files were deliberately left out of the commits.
- CI (`UWP (Xbox)`, run `39598826249`) was triggered by the push; its result was not awaited in this session.

#### 10. Known gaps and risks noticed but not changed

- **Tile launches (H5):** activation still happens only after the whole boot. If the app is killed from the tile,
  activate early, for example with `SDL_PumpEvents()` right after `SDL_CreateWindow`, and keep pumping during heavy
  init (scummvm's fix pattern).
- **DIP vs pixel:**
  - `ResizeFramebuffer` is fed `evt.window.data1/2` (window coordinates) instead of pixels (`app.cpp`,
    `SDL_EVENT_WINDOW_RESIZED`).
  - The Saturn image layout uses `SDL_GetWindowSize` (`app.cpp`, "Get window size").
  - Both are harmless while SDL3-uwp reports 1:1, but wrong at 200 % layout scaling.
- **`ResizeFramebuffer` RTV slots:** it allocates new RTV descriptors for the swap chain buffers without freeing the
  old ones. This is shared with desktop and the heap holds 256 slots.
- **`AudioSystem::Init`** does not store the requested sample rate and channel count. The defaults happen to match
  (44100 Hz, 2 channels).
- **`StallWatchdog` and `Trace()` at shutdown:**
  - The watchdog is destroyed before the emulator thread is joined, so a teardown hang is not watched.
  - `Trace()` reads `g_logFile` without synchronization; this is safe in practice, and is documented at the
    definition.
- **Series S:** XBSX2 says a 4K swap chain crashes it. SDL3-uwp already caps `SDL_GetWindowSizeInPixels` per model,
  and Ymir sizes its swap chain from that value.
- **Not adopted, noted for later:** clearing and presenting black on all back buffers right after creating the swap
  chain (XBSX2, xemu).

#### 11. Next step (user, with the Xbox)

1. F5 with **Debug**. Keep the whole Output window (save it as
   `docs/uwp/evidence/run-<date>-08-<summary>.raw.txt`).
2. F5 with **Release**.
3. If the UI shows, launch **from the tile** and download `ymir-uwp.log` from the Device Portal.
4. Read the result with "How to read run 08" in `docs/uwp/evidence/README.md`. The lines to look for:
   - `[Ymir gfx] D3D12 enhanced barriers reported: ...`;
   - `Dear ImGui backends initialized: ...`;
   - `[Ymir boot] frame 0: window ...`;
   - `[Ymir present] ...` lines;
   - any `[Ymir watchdog] ... stalled ...` line;
   - any `D3D12 WARNING`/`D3D12 ERROR` line.
5. If the *Welcome* modal shows up, continue with the checklist V1–V12 (step 12). When the boot is stable, remove the
   temporary instrumentation:
   - `YMIR_BOOT_TRACE`, `YMIR_PRESENT_TRACE` and `YMIR_UI_PHASE`;
   - the geometry trace;
   - `StallWatchdog`;
   - the enhanced-barrier trace.

   Keep `Trace()` itself and the ImGui init check.

#### 12. General Ymir documentation written in the same session

At the user's request, `docs/ymir/` was added: developer documentation of Ymir as a whole, not just the port. It was
written from a full read of the source and is in English, with Mermaid diagrams.

| Page | Contents |
|---|---|
| `README.md` | Index |
| `features.md` | Features |
| `saturn-hardware.md` | Hardware primer |
| `architecture.md` | Architecture |
| `core-system.md` | Core system side |
| `video.md` | Video |
| `audio.md` | Audio |
| `cd-and-media.md` | CD block and media |
| `frontend.md` | Frontend |
| `build-and-test.md` | Build and tests |
| `glossary.md` | Glossary |

- Each page ends with "Notes found while documenting", which lists possible bugs seen in the code, for example ROM
  cartridge save-state handling in `serdes/cereal_savestate.hpp`. **None of them were verified by running anything.**
- The Mermaid diagrams were **not** render-checked: the user ended the session before validation. If one does not
  render on GitHub, fix its syntax.
- `AGENTS.md`'s documentation map now lists `docs/ymir/`.

### Entrada do agente OpenCode #7 - 2026-10-21 (maquina de destino, COM Xbox, run 08 + correcao do assert do MIDI)

**Estado:** o boot agora **completa** no Xbox. D3D12, ImGui, XAudio2, thread do emulador e loop principal
comecam. A unica falha e um **assert de debug do CRT** disparado durante a inicializacao do MIDI.

#### 1. O que o run 08 provou

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

- **O backend XAudio2 funciona no Xbox.** `XAudio2Create` / `CreateMasteringVoice` / `CreateSourceVoice` / `Start`
  rodam sem travar. O deadlock do WASAPI esta resolvido.
- **O boot inteiro passa.** A geometria da janela esta correta: 1920x1080, nao minimizada, com foco de input,
  `io.DisplaySize` 1920x1080.
- **O CoreWindow foi ativado** (o splash sumiu), o que so acontece em `CoreWindow::Activate()`, disparado pelo
  primeiro `SDL_PollEvent()` do loop principal.
- Log completo em `docs/uwp/evidence/run-2026-10-21-08-midi-stdio-assert.raw.txt`.

#### 2. O crash: assert do CRT no destrutor do StdioSuppressor

```
Debug Assertion Failed!
File: minkernel\crts\ucrt\src\appcrt\lowio\dup2.cpp
Line: 67
Expression: ((unsigned)target_fh < _NHANDLE_)

ymir-sdl3.exe!util::StdioSuppressor::~StdioSuppressor() Line 40
ymir-sdl3.exe!app::services::MIDIService::DoInit(...) Line 178
```

Causa raiz (`apps/ymir-sdl3/src/util/stdio_suppress.cpp`):

```cpp
// construtor
m_fd_prev = dup(fileno(file));
freopen_s(&file, kNullFile, "w", file);   // <-- fecha a stream se falhar
// destrutor
dup2(m_fd_prev, fileno(m_file));           // <-- fileno() = -1 se a stream foi fechada
```

`freopen` **fecha a stream** quando falha. Sob UWP a reabertura falha, entao `fileno(stderr)` passa a ser `-1` e o
`_dup2(m_fd_prev, -1)` dispara o assert. **O `freopen` tambem fechava o descritor do pipe do log**, matando a thread
que drena o log.

Correcao aplicada: redirecionamento no **nivel do descritor**, sem `freopen`:
`dup` do descritor original -> `open("NUL")` -> `dup2` -> restaurar no destrutor. A stream nunca e fechada, o
descritor alvo e cacheado em `m_fd_target`, e a restauracao usa o valor cacheado em vez de `fileno()`. Se o null
device nao puder ser aberto, a supressao e simplesmente ignorada (com um `warn` no log) em vez de quebrar o app.

**Bonus:** o descritor do pipe do log nunca mais e fechado, entao a thread do log pump sobrevive a uma supressao.

#### 3. Pendencia: o crash pode nao ser a unica coisa

O `StdioSuppressor` e construido em `MIDIService::DoInit` para calar os avisos do RtMidi. O RtMidi no UWP pode
nao ter nenhum backend de MIDI utilizavel. Verificar no proximo run:
- se aparece `warn | StdioSuppress | Cannot open 'NUL' ...`, o null device nao e acessivel no AppContainer e a
  supressao nao acontece (ruido do RtMidi no log, sem consequencia);
- se nao aparece, a supressao funcionou.

O `WrapRtMidi` ja trata o caso de "nenhum backend" devolvendo um Noop (`midi_types.hpp:245-257`), entao nao ha
null-deref. **Nao ha outro assert conhecido no caminho.**

#### 4. Proximo passo (usuario, com o Xbox)

1. F5 com **Debug**. O assert do MIDI deve sumir. Salvar o Output em
   `docs/uwp/evidence/run-<data>-09-*.raw.txt`.
2. Conferir se o app **apresenta frames**: linhas `[Ymir present] ...` a cada 5 s.
3. Conferir se o watchdog acusa stall: `[Ymir watchdog] ... stalled ...`.
4. Se a UI aparecer (menu ImGui + modal *Welcome*), seguir o checklist V1-V12 do passo 12.
5. Apos o boot estavel, remover a instrumentacao temporaria (ver a entrada do Claude #12).

#### 5. Cobertura de teste adicionada

Novo alvo `ymir-sdl3-tests` (`tests/ymir-sdl3-tests/`), Catch2, no mesmo padrao do `ymir-core-tests`. Ele compila
as fontes do frontend que testa direto (sem linkar o app) e cobre o `util::StdioSuppressor`:

- a stream e silenciada enquanto o suppressor esta vivo e restaurada depois;
- a stream nunca e fechada, entao `fileno()` continua valido.

**Limitacao importante:** os testes estao **OFF no build UWP** (`Ymir_ENABLE_TESTS=OFF`) e o `catch2` do
`vcpkg.json` e apenas para `!uwp`. Portanto **um teste desktop nao pegaria o bug do `freopen`**, que so falha
sob UWP. O run no Xbox continua sendo a cobertura real das falhas exclusivas de UWP. O que o teste garante e a
invariavel nova (nunca fechar a stream).

#### 6. Estado do repositorio

- Branch `uwp/main`. Build local verde: `pwsh scripts/uwp/build.ps1 -Configuration Debug` (MSIX gerado).
- Commits desta sessao: `02b591d7` (correcao do assert) e `7dd73f74` (alvo de teste).
- **O pull do Claude e no-op**: os dois agentes trabalham na mesma working tree (`F:\workspace\Ymir`).
- O build local pode falhar com `LNK1201` (PDB travado) enquanto o VS estiver aberto: matar os `MSBuild`/`mspdbsrv`
  orfaos e **apagar** o `ymir-sdl3.pdb` (o lock e de escrita, o delete funciona).

---

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
| Build desktop (preset `Full-x86-64-AVX2-MSVC-Release`) e testes continuam passando | ✅ 14/14 na 0.4.2 (seção 1.1) |
| `scripts/uwp/package.ps1` → `out/dist/ymir-uwp_<ver>_x64.zip` (msix, cer, VCLibs, Install.ps1, INSTALL.md) + zip de símbolos | ✅ gerado e conferido |
| CI/CD `.github/workflows/uwp.yml` (build, artifact, release por tag `uwp-v*`) | ✅ primeiro run verde em 2026-10-20 (runner `windows-2025` com **Visual Studio Enterprise 2026**, versão 0.4.0.2, artifacts `ymir-uwp-0.4.0.2` e `-symbols`; esse run foi antes do rebase pra 0.4.2). A publicação de release por tag ainda não foi exercitada (passo 15) |
| Sincronizado com o upstream **0.4.2** (`8cdf0a53`) por rebase em 2026-10-20; build UWP Release limpo (`--fresh`, baseline nova do vcpkg) | ✅ |
| Fallback VS 2022 (preset `UWP-x86-64-AVX2-MSVC-VS2022`) | ⚠️ configurado, mas **não compilado**: a máquina de origem e o runner do GitHub têm VS 2026 |
| **Rodar o app** (PC ou Xbox) | ⚠️ **Xbox: the boot now completes** (D3D12, ImGui, XAudio2, emulator thread and main loop all start, correct window geometry) and then the process **hits a CRT debug assertion** in `util::StdioSuppressor::~StdioSuppressor()` during MIDI init (run 08). **Fixed in the tree, not yet verified on the console.** PC: not tested |

### 1.1 Resultado da regressão desktop
✅ Em 2026-10-20, depois do rebase pro upstream 0.4.2: o preset `Full-x86-64-AVX2-MSVC-Release` (Ninja + MSVC
14.51 + triplet `x64-windows`, baseline nova do vcpkg) compilou sem erros, e o `ctest` deu **14/14 testes OK**.

Antes do rebase eram 75 testes. A diferença vem do upstream, que removeu `ymir-dbg`, `ymir-headless` e
`ymir-dbg-commons` com os testes deles; não tem relação com o port.

### 1.2 Identidade do pacote (útil para achar arquivos)

| Campo | Valor |
|---|---|
| Identity Name | `Ymir.SaturnEmulator.UWP` |
| Publisher | `CN=CMake Test Cert` |
| **Package Family Name (PFN)** | `Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm` |
| App Id | `App` (PRAID = `Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm!App`) |
| Versão | `0.4.2.0` local (vem do `project(Ymir VERSION ...)` no CMake); no CI, `0.4.2.<run>` ou a da tag `uwp-v*` |
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
  MSIX:         ...\AppPackages\ymir-sdl3\ymir-sdl3_0.4.2.0_x64_Debug_Test\ymir-sdl3_0.4.2.0_x64_Debug.msix
  Dependency:   ...\Dependencies\x64\Microsoft.VCLibs.x64.Debug.14.00.appx
  Certificate:  ...\ymir-sdl3_0.4.2.0_x64_Debug.cer
  ```
- [ ] Warnings **esperados e inofensivos**:
  - `APPX0006 ... runFullTrust`;
  - `LNK4075 ... /INCREMENTAL`;
  - os vários `compile_hlsl_shader: This generator does not support depfiles`.
- [ ] Qualquer `error` está na seção 4 (T-03 a T-07).

### Passo 5: Build Release do pacote
- [ ] `pwsh scripts/uwp/build.ps1 -Configuration Release`
- [ ] **Esperado:** `...\ymir-sdl3_0.4.2.0_x64_RelWithDebInfo_Test\ymir-sdl3_0.4.2.0_x64_RelWithDebInfo.msix` e
  `Dependencies\x64\Microsoft.VCLibs.x64.14.00.appx`.
- [ ] **Use o Release pro Xbox.** O Debug depende do `Microsoft.VCLibs.140.00.Debug` e é bem mais lento.

### Passo 5.1 (opcional): Abrir no Visual Studio
- [ ] `pwsh scripts/uwp/open-vs.ps1`

  O Ymir é um projeto CMake: a solution e os `.vcxproj` são **gerados** em `out\build\<preset>\` e não ficam no git,
  porque têm caminhos absolutos da máquina. O script gera tudo e abre `out\build\UWP-x86-64-AVX2-MSVC\Ymir.slnx`.
- [ ] No VS: o `ymir-sdl3` já é o projeto de inicialização. Escolha `RelWithDebInfo` ou `Debug`, plataforma `x64`,
  e aperte **F5**. Isso instala e roda o app UWP no PC e exige o Modo de desenvolvedor do Windows.
- [ ] **Não** use *File > Open > Folder* para o build UWP. Depois de mudar qualquer `CMakeLists.txt`, rode o script
  de novo e recarregue a solution quando o VS pedir.

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
- [ ] Na pasta `...\ymir-sdl3_0.4.2.0_x64_RelWithDebInfo_Test\`, rode `pwsh .\Install.ps1`.

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
- [ ] Confira que só o `UWP (Xbox)` está ativo: `gh workflow list -R marcelofrau/Ymir --all`. Os workflows do upstream
  ficam `disabled_manually`, porque este fork é só UWP. Se aparecer algum workflow novo do upstream ativo depois de um
  rebase, desligue com `gh workflow disable <id> -R marcelofrau/Ymir`.
- [ ] Dispare um build: `gh workflow run uwp.yml -R marcelofrau/Ymir --ref uwp/main`. Acompanhe com
  `gh run watch -R marcelofrau/Ymir`.
- [ ] **Esperado:** o job "Build UWP package (x64)" termina verde e o run tem o artifact `ymir-uwp-0.4.2.<run>`.
  O passo "Show toolchain" mostra qual VS foi usado.
- [ ] Se falhar, baixe o log (`gh run view --log-failed -R marcelofrau/Ymir`) e procure na seção 4. Nos erros de
  compilação com VS 2022, veja T-01.
- [ ] **Release:** depois do teste no Xbox (passo 12), crie e envie a tag:
  ```powershell
  git tag uwp-v0.4.2.1
  git push origin uwp-v0.4.2.1
  ```
  **Esperado:** aparece um GitHub Release "Ymir UWP 0.4.2.1" com `ymir-uwp_0.4.2.1_x64.zip` e o zip de símbolos.
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

> **ATENÇÃO — receita antiga removida.** A versão anterior deste item mandava trocar `DXGI_SCALING_NONE` por
> `DXGI_SCALING_STRETCH` sob UWP, alegando que "o CoreWindow não aceita `DXGI_SCALING_NONE`". **Isso nunca foi
> verificado e o run real de 2026-10-20 no Xbox desmentiu a premissa:** o log mostra
> `Direct3D 12 graphics context initialized successfully`, ou seja, a swapchain é criada com sucesso. A doc da
> Microsoft também **não** proíbe `DXGI_SCALING_NONE` com `CreateSwapChainForCoreWindow` (o exemplo oficial de
> foreground layer usa `DXGI_SCALING_NONE`). Não aplique aquela troca sem um log que a justifique.

- **Diagnóstico primeiro:** faça o app logar o `HRESULT` da falha, além de `size`, `scaling`, `flags`, `swapEffect` e
  `bufferCount` (já instrumentado em `gfx_context_impl_d3d12.cpp` pelo commit `35e49d8c`). Sem o `HRESULT` não dá
  para distinguir device, tamanho e modo de escalonamento.
- **Causas plausíveis:** o device D3D12 não foi criado (veja T-14); ou o tamanho veio 0x0 de
  `SDL_GetWindowSizeInPixels` — nesse caso logue `windowWidth`/`windowHeight` e, se for 0, use 1920x1080 como fallback.
- **Se o app fica no splash sem log de erro:** não é este item. Veja a seção 0.1 — o ponto de parada medido fica
  **depois** da criação do contexto gráfico.

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

### T-18: `fatal: Unable to create '.git/index.lock': File exists`
- **Causa:** um processo git anterior morreu e deixou o lock. Na máquina de origem isso aconteceu algumas vezes
  durante builds longos. O CMake do Ymir não chama git, então o culpado é algo externo.
- **Correção:**
  1. Confirme que **nenhum** git está rodando: `Get-Process git -ErrorAction SilentlyContinue` não retorna nada.
     Feche IDEs com integração git, se houver.
  2. Remova **só esse arquivo**: `Remove-Item .git\index.lock`.
  3. Repita o comando git.

---

## 5. Registro de execução (preencha durante o trabalho)

| Data | Passo | Resultado | Notas / erro / log anexado |
|---|---|---|---|
| 2026-10-20 | 1–6 | OK (máquina de origem) | `.msix` Debug e Release sem erros; regressão desktop 75/75 |
| 2026-10-20 | 8 da seção 8 (rebase) | OK | Rebase pro upstream 0.4.2 (285 commits). Conflitos: `CMakeLists.txt` raiz (bloco de versão/feature flags reorganizado), `apps/ymir-sdl3/CMakeLists.txt` (macros) e `video_settings_view.cpp` (o upstream passou a mostrar o toggle de HW sempre; mudança do port removida). Backup pré-rebase em `backup/uwp-main-pre-0.4.2` (só local) |
| 2026-10-20 | 15 (parcial) | OK | Run 39396157010 do `uwp.yml` verde com VS 2026 no runner; artifact de 5,3 MB |
| 2026-10-20 | 12 (máquina de destino) | **PARCIAL** | **Primeiro boot real no Xbox.** O crash de `SDL_GetCurrentDirectory()` (`39aa6355`) está resolvido e o mirror de log funciona. O boot chega ao `App::Run()`, inicializa o D3D12 com sucesso e para no splash **dentro de `AudioSystem::Init()`** (`SDL_OpenAudioDeviceStream`) — o loop principal nunca é alcançado. Detalhes nas seções 0.1 (entradas #3 e #4) e em `docs/uwp/evidence/` |
| 2026-10-20 | 12 (máquina de destino) | aguardando run 05 | Build Debug com marcas `[Ymir audio]` compilado; aguardando o usuário rodar no Xbox para confirmar se a parada é dentro de `SDL_OpenAudioDeviceStream` |
| 2026-10-20 | 12 (target machine) | run 05/06 | WASAPI deadlock confirmed; worker-thread workaround refuted; XAudio2 backend implemented (`071639c9`) |
| 2026-10-21 | 12 (target machine) | **PARTIAL** (run 07) | Debug, F5: the splash is dismissed (the main loop is reached, so XAudio2 works) and the screen stays black. No log kept. Instrumentation for run 08 added (section 0.1) |
| 2026-10-21 | 12 (target machine) | **PARTIAL** (run 08) | Debug, F5: **the boot completes** (D3D12, ImGui, XAudio2, emulator thread, main loop, window 1920x1080 not minimized, `io.DisplaySize` 1920x1080). Then a **CRT assertion** (`dup2.cpp:67`, `((unsigned)target_fh < _NHANDLE_)`) in `util::StdioSuppressor::~StdioSuppressor()` during MIDI init. Root cause: `freopen` closes the stream when it fails, so `fileno()` returns -1. Fixed by redirecting at the descriptor level. Log: `docs/uwp/evidence/run-2026-10-21-08-midi-stdio-assert.raw.txt` |
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

Depois do rebase:
1. Refaça os passos 4 e 5.
2. O passo 6 (regressão desktop) é opcional; o desktop é responsabilidade do upstream.
3. Rode `gh workflow list -R marcelofrau/Ymir --all` e desligue workflows novos do upstream que estejam ativos.
