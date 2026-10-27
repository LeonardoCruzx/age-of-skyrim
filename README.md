# Age of Skyrim

Mod de gestão de facções e conquista territorial para Skyrim SE/AE (um único DLL, via SKSE + CommonLibSSE-NG).

[![Build](https://github.com/LeonardoCruzx/age-of-skyrim/actions/workflows/build.yml/badge.svg)](https://github.com/LeonardoCruzx/age-of-skyrim/actions/workflows/build.yml)

**Estado atual — Fase 1 (Core & Banners), esqueleto inicial.** Compila no CI (MSVC), mas **ainda não foi testado no jogo**.

**Documentação:** [Roadmap](docs/ROADMAP.md) · [Decisões (ADRs)](docs/adr/README.md) · [Roteiros de teste](TESTES.md)

**Idiomas:** inglês e português do Brasil. `settings.json` → `"language": "en" | "pt-br" | "auto"`.
Textos em `Data/SKSE/Plugins/AgeOfSkyrim/Translations/*.json`; o CI falha se os dois idiomas divergirem
(`tools/check_translations.py`).

## Pré-requisitos (Windows)

- Visual Studio 2022 com a carga de trabalho *Desenvolvimento para desktop com C++*
- CMake ≥ 3.21 e Git
- [vcpkg](https://github.com/microsoft/vcpkg) clonado, com a variável de ambiente `VCPKG_ROOT` apontando para ele
- SKSE64 e Address Library for SKSE Plugins instalados no jogo

## Build

```powershell
git clone --recursive <url-do-seu-repo> AgeOfSkyrim
cd AgeOfSkyrim
cmake --preset windows-msvc
cmake --build --preset release
```

Para copiar DLL + JSONs automaticamente para a pasta do mod (MO2/Vortex) a cada build:

```powershell
setx AOS_DEPLOY_DIR "D:\MO2\mods\AgeOfSkyrim"
```

### Build pelo CI (sem Visual Studio)

Cada push em `main` compila o DLL no GitHub Actions (`.github/workflows/build.yml`, runner `windows-2022`).
Em *Actions → Build → (run) → Artifacts*, baixe `AgeOfSkyrim-<sha>`: o zip já está no layout do mod
(`SKSE/Plugins/AgeOfSkyrim.dll` + JSONs) e pode ser instalado direto no MO2/Vortex.
Se o build falhar, o fim dos logs do vcpkg/CMake aparece como anotações no *Summary* do run.

O log fica em `Documents\My Games\Skyrim Special Edition\SKSE\AgeOfSkyrim.log`.

## Como funciona o controle de banners

1. **No Creation Kit** (`AgeOfSkyrim.esp`): em cada forte/torre/vila, coloque um objeto de bandeira **por estilo
   de heráldica** (imperial, stormcloak, ...). Marque cada referência como **Persistent** e **Initially Disabled**.
2. **Em `Data/SKSE/Plugins/AgeOfSkyrim/Territories/*.json`**: declare os estilos e, por território, quais
   referências pertencem a cada estilo (`"Plugin.esp|0xID"`, ID *local* — sem o byte de load order).
3. **Em tempo de execução**: `SetTerritoryOwner(território, estilo)` atualiza o estado (salvo no cosave) e agenda,
   na thread principal, `Enable()` nos banners do dono e `Disable()` em todos os outros. Nenhum script Papyrus.

Por que "estilos" e não "uma bandeira por facção"? Facções criadas pelo jogador são ilimitadas, mas referências
pré-colocadas no CK são finitas. Cada facção escolhe um estilo de um conjunto fechado (os que você modelar/colocar).

### Objetos criados em runtime (sem ESP)
Além de refs pré-colocadas (`"Plugin.esp|0xID"`), cada item de `banners` pode ser
`{ "place": "Plugin.esp|0xID", "offset": [direita, frente, cima] }`: o DLL cria o objeto (hoje relativo ao jogador)
e guarda a referência no cosave (registro `PLAC`). Nada de editar células vanilla no ESP.

### Teste rápido sem UI
`settings.json` → `debug`: **F10** (`hotkey`, DIK 68) alterna o dono de `debug.territory`; **F11**
(`selfTestHotkey`, DIK 87) roda o autoteste (PASS/FAIL na tela e no log) e, depois de salvar e carregar,
confere a persistência. Roteiro passo a passo em [TESTES.md](TESTES.md).
**F8** (`stockHotkey`, DIK 66) mostra o inventário do jogador por categoria de estoque; **F9** (`claimHotkey`,
DIK 67) finca um estandarte (Free Claim) à frente do jogador.

### Estoque por categoria (`Resources.json`)
Categorias ordenadas (a ordem é a prioridade) com nome `{en, pt-br}` e regras `forms` (form exato),
`keywords` (keywords vanilla `VendorItem*`; itens de outros mods entram sozinhos) e `formTypes` (fallback).
Contagem só ao fechar o baú observado (M6). Detalhes na [ADR 0017](docs/adr/0017-estoque-classificacao-por-keyword.md).

### Free Claim (`settings.json` → `claim`)
`banner` (objeto base, padrão provisório Iron Sword), `minDistance` (unidades; 70 ≈ 1 m), `maxClaims`,
`maxSlopeDegrees`, `forwardDistance`. Claims ficam no cosave (registro `CLAM`). Ver [ADR 0018](docs/adr/0018-free-claim-raycast.md).

## Estrutura

| Arquivo | Responsabilidade |
|---|---|
| `src/Core/Ids.h` | IDs por hash FNV-1a (estáveis entre versões do JSON) |
| `src/Core/Config.*` | Leitura dos JSONs, validação, resolução de `Plugin.esp\|0xID` |
| `src/Core/State.*` | Dono de cada território + persistência no cosave (registro `TERR` v1) |
| `src/Core/I18n.*` | Textos para o jogador (en / pt-br), fallback para inglês, notificações na tela |
| `src/Core/Timer.h` | Tempo das operações no log (`[tempo] ...`) |
| `src/World/Placement.*` | Objetos criados em runtime + refs no cosave (registro `PLAC` v1) |
| `src/Debug/SelfTest.*` | Autoteste F11 e checagem de persistência (registro `TEST` v1) |
| `src/Stock/Stock.*` | Estoque por categoria (`Resources.json`), recontagem ao fechar baú observado, debug F8 |
| `src/World/Claims.*` | Free Claim: raycast + alinhamento à normal, limites, registro `CLAM` v1, debug F9 |
| `src/Banner/BannerController.*` | Único ponto que chama `Enable()/Disable()`; `SetTerritoryOwner()` |
| `src/Events/Events.*` | Retentativa ao carregar células; hotkeys de debug (F10/F11) |
| `src/Main.cpp` | Entrada SKSE, log, mensagens; um laço lê o cosave e despacha cada registro ao seu módulo |

## Pendências conhecidas da Fase 1

- [x] Compilar (CI)
- [ ] Validar no jogo — roteiro M2 em [TESTES.md](TESTES.md) (ver "Pontos a verificar" abaixo)
- [ ] Criação de facção (nome, estilo, cor) com persistência no cosave
- [x] *Free Claim*: raycast com alinhamento à normal do terreno (debug F9; falta virar território — M3/M4)
- [ ] Ligar a conquista real (captura de local) a `SetTerritoryOwner`

## Pontos a verificar no primeiro teste

- `TESObjectREFR::Enable(false)` / `Disable()` em refs de células descarregadas (esperado: ok para refs persistentes).
- Que o vcpkg baseline do `vcpkg.json` (o mesmo usado pela CommonLibSSE-NG 3.x) resolve `spdlog`/`nlohmann-json` sem conflito.
- Ordem de eventos: `Revert → Load (cosave) → kPostLoadGame → ApplyAll`.

## Docker Compose / Devcontainer (Claude com conta pessoal)

```bash
git clone --recursive <url-do-seu-repo> && cd AgeOfSkyrim
docker compose up -d --build
docker compose exec dev bash     # dentro: rode `claude` e entre com a conta PESSOAL
docker compose down              # para o container (o login permanece no volume)
```

Ou, no VS Code: *Dev Containers: Reopen in Container* (usa o mesmo `docker-compose.yml`).

O container serve para **editar o código e usar o Claude Code**; o DLL só compila no Windows com MSVC
(veja "Build"), e o clangd no Linux tem diagnósticos parciais por causa dos headers específicos de MSVC.

Isolamento de conta: o login do Claude fica no volume `aos-claude-config`
(`CLAUDE_CONFIG_DIR=/home/vscode/.claude`), sem montar o `~/.claude` do host. Para apagar o login:
`docker compose down -v`.
