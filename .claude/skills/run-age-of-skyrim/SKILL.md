---
name: run-age-of-skyrim
description: Build, test, check CI, download the built DLL artifact, package the FOMOD release zip and read performance from AgeOfSkyrim.log for the Age of Skyrim SKSE plugin. Use when asked to run, build, compile, test, verify CI, get the artifact, package/release, or check perf of this mod.
---

# Rodar / verificar o Age of Skyrim

Plugin SKSE (DLL Windows, C++23 + CommonLibSSE-NG). **Não roda neste container:** o DLL só compila com MSVC no
GitHub Actions (`build.yml`, `windows-2022`) e só executa dentro do Skyrim SE/AE no Windows; não há modo headless
nem como subir o jogo aqui. O que dá para dirigir daqui é a esteira inteira, com um driver só:

`.claude/skills/run-age-of-skyrim/driver.sh` (caminhos relativos à raiz do repo; o driver faz `cd` para a raiz).

## Pré-requisitos
`python3`, `curl`, `unzip`, `git` (já no devcontainer). Token do GitHub: `GH_TOKEN` ou a credencial do git
(`git credential fill`); sem token a API anônima dá 60 req/h e **baixar artefato não funciona**.

## Agente: o caminho principal

```bash
D=.claude/skills/run-age-of-skyrim/driver.sh
$D check                            # traduções en/pt-br + 30 testes Python + fomod/patches (≈1 s)
git push origin <branch>            # o "build" é o push: CI dispara em main e em branches m*-*
$D ci <sha>                         # espera o run do Build (5–35 min) e, se falhou, imprime os erros do MSVC
$D artifact <sha>                   # baixa e extrai AgeOfSkyrim-<sha> em dist/artifact (DLL + data)
$D package dist/artifact 0.1.0      # dist/AgeOfSkyrim-0.1.0.zip no layout FOMOD (não altera o repo)
$D perf /caminho/absoluto/AgeOfSkyrim.log   # tempos [tempo] por operação vs metas (1 ms evento, 5 ms tick)
```

Ciclo de uma mudança em `src/`: `$D check` → commit → push numa branch `m<N>-<slug>` → `$D ci <sha>` →
corrigir o que o MSVC apontar → repetir até `completed / success`.

A **verificação no jogo** é sempre do usuário (Windows). O que pedir: o `AgeOfSkyrim.log` de
`Documentos\My Games\Skyrim Special Edition\SKSE\`; rode `$D perf` nele e leia as linhas de resolução de
FormIDs/objetos que cada módulo registra.

## Humano (Windows)
Baixar o artefato do run verde em Actions → instalar o zip no MO2/Vortex → abrir pelo SKSE. Roteiro em `TESTES.md`.

## Gotchas
- **Há dois workflows por commit:** `Tools` (ubuntu, ~30 s) e `Build` (windows, o que importa). Escolher "o
  primeiro run do SHA" pega o Tools e dá falso verde; o driver filtra por `name == "Build"`.
- O Build leva 5–35 min dependendo do cache do vcpkg (chave = commit do vcpkg + hash do `vcpkg.json`): mudar
  o `vcpkg.json` invalida o cache e o run fica longo.
- `package` sem o driver falha com `CHANGELOG.md sem a seção '## [0.1.0]'`: o `package_release.py` exige a seção
  da versão; o driver monta o zip numa cópia temporária com `[Não lançado]` renomeado.
- `package` avisa `language = 'pt-br'` e `debug.enabled = true`: são os valores de desenvolvimento do
  `settings.json`; para release real devem ser `auto` e `false` (`docs/RELEASE.md`).
- `perf` com caminho relativo procura a partir da raiz do repo (o driver faz `cd`): passe caminho absoluto.
- O submódulo `extern/CommonLibSSE-NG` pode vir vazio em worktrees novos; os headers estão no checkout
  principal (`/workspaces/AgeOfSkyrim/extern/CommonLibSSE-NG/include`). Não há como compilar C++ localmente
  (sem MSVC/Windows SDK); consulte os headers para conferir assinaturas antes do push.
- Teclas de debug alocadas (DIK): F3 claim, F4 conquista, F6 economia, F7 rota, F8 estoque, F10 dono, F11
  autoteste, NumPad1–4 reino, NumPad5–6 guarnição/melhorias, NumPad7–8 IA/diplomacia, NumPad9 mapa, NumPad0 API.
  F5/F9 são quicksave/quickload do jogo.

- A credencial do git aqui vem do helper do VS Code (`REMOTE_CONTAINERS_IPC`); num shell sem essas variáveis
  (`env -i`, cron, outro terminal) o `git credential fill` falha e o driver segue **anônimo**: `ci` funciona,
  `artifact` não. Exporte `GH_TOKEN` nesse caso.

## Troubleshooting
- `Baixar artefato exige token`: sem `GH_TOKEN` e sem o helper do VS Code; exporte `GH_TOKEN` (escopo actions:read).
- `run none: none / none` no `ci`: o push ainda não criou o run (espere alguns segundos) ou a branch não casa
  com `main`/`m*-*`.
- `Build de <sha> não terminou com sucesso` no `artifact`: rode `$D ci <sha>` primeiro.
- `ALERTA ... > meta` no `perf` com `--strict` sai com código 1 (útil em CI); sem `--strict` sai 0.
