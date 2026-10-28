# Age of Skyrim

Mod de gestão de facções e conquista territorial para Skyrim SE/AE (um único DLL, via SKSE + CommonLibSSE-NG).

[![Build](https://github.com/LeonardoCruzx/age-of-skyrim/actions/workflows/build.yml/badge.svg)](https://github.com/LeonardoCruzx/age-of-skyrim/actions/workflows/build.yml)

**Estado atual — Fase 1 (Core & Banners), esqueleto inicial.** Compila no CI (MSVC), mas **ainda não foi testado no jogo**.

**Documentação:** [Roadmap](docs/ROADMAP.md) · [Decisões (ADRs)](docs/adr/README.md) · [Roteiros de teste](TESTES.md) · [Referências](docs/REFERENCIAS.md) · [Comparativo com Conquest of Skyrim](docs/COMPARATIVO-CONQUEST-OF-SKYRIM.md)

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
(`SKSE/Plugins/AgeOfSkyrim.dll` + JSONs, `AgeOfSkyrim.esp`, `Scripts/*.pex`, `Interface/Translations/`) e pode ser
instalado direto no MO2/Vortex. **Ative o `AgeOfSkyrim.esp`** (ESL: não ocupa slot normal) para ter o MCM.
O CI também compila os scripts Papyrus com o [Caprica](https://github.com/Orvid/Caprica) (sem Creation Kit,
[ADR 0035](docs/adr/0035-papyrus-compilado-no-ci-caprica.md)), gera o ESP (`tools/build_esp.py`,
[ADR 0034](docs/adr/0034-esp-gerado-sem-creation-kit.md)) e roda `python -m unittest discover -s tests`.
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

### MCM (SkyUI, opcional)
Com SkyUI 5.x instalado aparece **Age of Skyrim** no menu de configuração: Facção (criar/renomear, estilo, cor),
Territórios (lista paginada + dono; com `debug.enabled` dá para trocar o dono), Sede e Estoque, Economia e Governo
(em breve) e Depuração (atalhos e ações). Os textos seguem o `language` do `settings.json`
([ADR 0033](docs/adr/0033-mcm-skyui-casca-fina.md)). Sem SkyUI o mod funciona igual, só sem o menu.

### API para outros mods (`settings.json` → `api`)
`papyrusEvents` (ModEvents `AoS_*`) e `allowOwnerChanges` (outros mods podem trocar donos).
Detalhes e eventos reservados na [ADR 0032](docs/adr/0032-api-publica-para-outros-mods.md).

**Plugin SKSE (C++)** — copie [`include/AgeOfSkyrimAPI.h`](include/AgeOfSkyrimAPI.h):

```cpp
#include "AgeOfSkyrimAPI.h"

AgeOfSkyrimAPI::IAgeOfSkyrim* g_aos = nullptr;

void OnAoSEvent(const AgeOfSkyrimAPI::Event& e, void*)  // sempre na thread principal
{
    char name[128];
    g_aos->GetTerritoryName(e.territory, name, sizeof(name));
    SKSE::log::info("{} agora é da facção 0x{:08X}", name, e.faction);
}

void OnMessage(SKSE::MessagingInterface::Message* a_msg)
{
    if (a_msg->type == SKSE::MessagingInterface::kPostLoad) {  // ou kPostPostLoad
        g_aos = AgeOfSkyrimAPI::RequestInterface();            // nullptr = Age of Skyrim ausente/antigo
        if (g_aos) {
            g_aos->AddListener(AgeOfSkyrimAPI::EventType::kTerritoryChanged, OnAoSEvent, nullptr);
        }
    }
}
```

Sem CommonLib: envie `kMessageRequestInterface` com um `InterfaceRequest` para `"AgeOfSkyrim"` via
`MessagingInterface::Dispatch`, ou chame o export `AoS_GetInterface(versão)` do `AgeOfSkyrim.dll`.

**Papyrus** — natives em [`AgeOfSkyrim.psc`](data/Scripts/Source/AgeOfSkyrim.psc) (facções e territórios por chave
de texto, `""` = neutro):

```papyrus
Event OnInit()
    RegisterForModEvent("AoS_TerritoryChanged", "OnAoSTerritoryChanged")
EndEvent

Event OnAoSTerritoryChanged(String asEventName, String asTerritory, Float afValue, Form akSender)
    String owner = AgeOfSkyrim.GetTerritoryOwner(asTerritory)
    Debug.Trace(AgeOfSkyrim.GetTerritoryName(asTerritory) + " -> " + AgeOfSkyrim.GetFactionName(owner))
EndEvent
```

## Estrutura

| Arquivo | Responsabilidade |
|---|---|
| `src/Core/Ids.h` | IDs por hash FNV-1a (estáveis entre versões do JSON) |
| `src/Core/Config.*` | Leitura dos JSONs, validação, resolução de `Plugin.esp\|0xID` |
| `src/Core/State.*` | Facção dona de cada território + persistência no cosave (registro `TERR` v2, migra v1) |
| `src/Core/Factions.*` | Facções pré-definidas (`Factions/*.json`) + do jogador (registro `FACT` v1); dono → estilo |
| `src/Core/I18n.*` | Textos para o jogador (en / pt-br), fallback para inglês, notificações na tela |
| `src/Core/Timer.h` | Tempo das operações no log (`[tempo] ...`) |
| `src/World/Discovery.*` | Territórios descobertos dos `BGSLocation` (tipo, província, posição, vizinhos) |
| `src/World/Placement.*` | Objetos criados em runtime + refs no cosave (registro `PLAC` v1) |
| `src/Debug/SelfTest.*` | Autoteste F11 e checagem de persistência (registro `TEST` v1) |
| `src/Stock/Stock.*` | Estoque por categoria (`Resources.json`), recontagem ao fechar baú observado, debug F8 |
| `src/World/Claims.*` | Free Claim: raycast + alinhamento à normal, limites, registro `CLAM` v1, debug F9 |
| `src/Banner/BannerController.*` | Único ponto que chama `Enable()/Disable()`; `SetTerritoryOwner()` |
| `src/Events/Events.*` | Retentativa ao carregar células; hotkeys de debug (F10/F11) |
| `include/AgeOfSkyrimAPI.h` | API pública C++ versionada (interface, eventos, pedido por mensagem SKSE) |
| `src/Api/ModApi.*` | Implementação da interface, mensagem 'AOSI', export `AoS_GetInterface`, ganchos para M6/M12/M13/M18 |
| `src/Api/Events.*` | Registro central de eventos: callbacks C++ + ModEvents `AoS_*`, sempre na thread principal |
| `src/Papyrus/Api.*` | Natives do `AgeOfSkyrim.psc` (MCM e mods só-script) |
| `data/Scripts/Source/*.psc` | `AgeOfSkyrim.psc` (natives) e `AgeOfSkyrimMCM.psc` (MCM do SkyUI) |
| `tools/papyrus/` | Stubs de compilação próprios + flags (Caprica no CI); não vão no pacote |
| `tools/build_esp.py` | Gera o `AgeOfSkyrim.esp` (ESL, quest do MCM) — testado em `tests/test_build_esp.py` |
| `tools/gen_skyui_translations.py` | `Interface/Translations/AgeOfSkyrim_*.txt` a partir dos JSON de tradução |
| `src/Main.cpp` | Entrada SKSE, log, mensagens; um laço lê o cosave e despacha cada registro ao seu módulo |

## Pendências conhecidas da Fase 1

- [x] Compilar (CI)
- [ ] Validar no jogo — roteiro M2 em [TESTES.md](TESTES.md) (ver "Pontos a verificar" abaixo)
- [x] Criação de facção (nome, estilo, cor) com persistência no cosave (M3; UI vem no MCM)
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
