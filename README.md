# Age of Skyrim

Mod de gestão de facções e conquista territorial para Skyrim SE/AE (um único DLL, via SKSE + CommonLibSSE-NG).

[![Build](https://github.com/LeonardoCruzx/age-of-skyrim/actions/workflows/build.yml/badge.svg)](https://github.com/LeonardoCruzx/age-of-skyrim/actions/workflows/build.yml)

**Estado atual — Fase 1 (Core & Banners), esqueleto inicial.** Compila no CI (MSVC), mas **ainda não foi testado no jogo**.

**Documentação:** [Roadmap](docs/ROADMAP.md) · [Decisões (ADRs)](docs/adr/README.md) · [Roteiros de teste](TESTES.md) · [Referências](docs/REFERENCIAS.md) · [Comparativo com Conquest of Skyrim](docs/COMPARATIVO-CONQUEST-OF-SKYRIM.md) · [Changelog](CHANGELOG.md) · [Como lançar](docs/RELEASE.md) · [Performance](docs/PERFORMANCE.md)

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

### Release, FOMOD e patches
Uma tag `v*` dispara `.github/workflows/release.yml`: reaproveita o artefato do Build do mesmo commit, monta o
zip FOMOD (`fomod/` + `00 Core/` + `patch_<nome>/` de cada `patches/<nome>/`) com `tools/package_release.py` e
publica o GitHub Release com a seção da versão do `CHANGELOG.md`. Passo a passo em [docs/RELEASE.md](docs/RELEASE.md).
Patches de compatibilidade são só JSON (formato em [patches/README.md](patches/README.md)).
Ferramentas python (sem dependências): `python3 -m unittest discover -s tests` (também no workflow `Tools`) e
`python3 tools/perf_report.py AgeOfSkyrim.log` (resumo dos `[tempo]`, [docs/PERFORMANCE.md](docs/PERFORMANCE.md)).

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
**F8** (`stockHotkey`, DIK 66) mostra o inventário do jogador por categoria de estoque (e o estoque da sede, se
houver); **F3** (`claimHotkey`, DIK 61) finca um estandarte (Free Claim) à frente do jogador; **F4**
(`conquestHotkey`, DIK 62) conquista o território do local atual sem precisar limpar (fora de um território, usa
`debug.territory`).

### Estoque por categoria (`Resources.json`)
Categorias ordenadas (a ordem é a prioridade) com nome `{en, pt-br}` e regras `forms` (form exato),
`keywords` (keywords vanilla `VendorItem*`; itens de outros mods entram sozinhos) e `formTypes` (fallback).
Contagem só ao fechar o baú observado (M6). Detalhes na [ADR 0017](docs/adr/0017-estoque-classificacao-por-keyword.md).

### Economia e rotas (`Economy.json`)
Recursos (`id`, `category` de `Resources.json`, `item` real), produção por hora por tipo de território,
`factionMultiplier`, `maxCatchUpHours` e bloco `routes` (`maxRoutes`, `baseGoldPerHour`, `goldPer10000Units`,
`maxDistance`, `blockRadius`, `typeMultiplier`). Outros mods: `Economy/*.json`. Debug: **F6** (`economyHotkey`,
DIK 64) força `economyHours` horas de economia e mostra o resumo; **F7** (`routeHotkey`, DIK 65) cria uma rota de
teste. Ver ADRs [0026](docs/adr/0026-tick-por-hora-orientado-a-evento.md),
[0027](docs/adr/0027-economia-producao-deposito-consumo.md) e [0028](docs/adr/0028-rotas-de-comercio.md).

### Guarnição (`Garrison.json` + `settings.json` → `garrison`)
Cada território com dono tem uma guarnição **numérica** (`initial`/`max` por tipo, reposição/deserção por hora,
manutenção `upkeep` por soldado). Os soldados só viram atores perto do jogador: `enabled`, `maxActors` (teto global),
`spawnPerFrame`, `spawnRadius`, `spreadRadius`. Tropas por facção/estilo em `troops` (FormID/EditorID ou escolha
automática por armadura). Outros mods: `Garrison/*.json`. Debug: **NumPad5** (`garrisonHotkey`, DIK 76) mostra a
guarnição do território atual e chama os soldados para perto de você. Ver ADRs
[0043](docs/adr/0043-guarnicao-numerica-materializada.md) e [0044](docs/adr/0044-tropas-bases-vanilla-outfit-faccoes.md).

### Melhorias (`Upgrades.json`)
Muralhas, quartel, torre de vigia, armazém, mercado e forja, níveis I–III, com custo em itens do baú da sede (por
categoria do estoque) e tempo de obra em horas de jogo; status `defense`, `capacity`, `income`, `vision`,
`garrisonMax`; objetos visuais por nível em `visuals`. Outros mods: `Upgrades/*.json`. Debug: **NumPad6**
(`upgradeHotkey`, DIK 77) inicia a próxima melhoria do território atual (ou da sede); `upgradeIgnoreCost: true`
dispensa o custo. Ver [ADR 0045](docs/adr/0045-melhorias-implementacao.md).

### Free Claim (`settings.json` → `claim`)
`banner` (objeto base, padrão provisório Iron Sword), `minDistance` (unidades; 70 ≈ 1 m), `maxClaims`,
`maxSlopeDegrees`, `forwardDistance`. Claims ficam no cosave (registro `CLAM`). Ver [ADR 0018](docs/adr/0018-free-claim-raycast.md).

### Conquista (`settings.json` → `conquest`)
Limpar um local (todos os inimigos mortos) passa o território para a sua facção — criada na hora, se você ainda não
tem uma. `enabled`, `requirePlayerInLocation` (false = vale também o local de onde acabou de sair), `onlyFromNeutral`,
`exclude` (`"Plugin.esp|0xID"` de `BGSLocation` ou id de território), `excludePlugins` (para mods de guerra civil),
`defaultStyle`, `defaultColor`. Ver [ADR 0022](docs/adr/0022-conquista-implementacao.md).

### Sede (`Seats/*.json` + `settings.json` → `seat`)
Conquistar uma candidata pergunta se você quer fundar (ou mudar) a sede ali; baú de estoque e mesa de guerra nascem
à sua frente. Candidatas: regra `auto` (N fortes/torres por província) + lista explícita + `exclude`. Settings:
`askBeforeFounding`, `chest`/`warTable` (`"auto"` ou `"Plugin.esp|0xID"`), `chestOffset`/`warTableOffset`.
Cosave `SEAT` v1. Ver [ADR 0023](docs/adr/0023-sede-implementacao.md) e [0025](docs/adr/0025-objetos-base-da-sede.md).
Claims (F3) viram territórios do tipo `claim` da sua facção ([ADR 0024](docs/adr/0024-claims-como-territorios.md)).

### Reino: doutrina, população e títulos (`settings.json` → `realm`)
`doctrineCooldownHours` (espera entre trocas de doutrina, horas de jogo; a 1ª escolha é livre) e `announceTitles`.
Dados em `Doctrines/*.json` (doutrinas, cargos, governador), `Population/*.json` e `Titles/*.json`, todos
extensíveis por outros mods (id repetido num arquivo posterior substitui). Debug: **NumPad1** (`realmHotkey`) resumo do
reino, **NumPad2** (`realmHoursHotkey`) simula `realmDebugHours` horas, **NumPad3** (`realmDoctrineHotkey`) próxima doutrina,
**NumPad4** (`realmConquerHotkey`) entrega os `realmConquerCount` territórios mais próximos à sua facção.
Ver ADRs [0036](docs/adr/0036-governo-doutrina-cargos.md)–[0039](docs/adr/0039-reino-modulo-integracao.md).

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

### Mesa de guerra / mapa tático (`settings.json` → `warMap`)
Ativar a mesa de guerra da sede abre o mapa tático: com o [PrismaUI](https://www.prismaui.dev/) instalado, uma view
HTML com o mapa (territórios por tipo e dono, províncias, vizinhança, rotas, estoque da sede, alvos); sem ele, menus
vanilla com as mesmas ações. Settings: `enabled`, `usePrismaUI`, `view`, `pauseGame`, `pageSize`, `nearbyRadius`,
`maxTargets`; debug `warMapHotkey` (NumPad9). Outros módulos acrescentam dados por `WarMap::AddProvider`.
Schema do snapshot em [docs/warmap-schema.md](docs/warmap-schema.md). Ver ADRs [0051](docs/adr/0051-prismaui-opcional-interface-minima.md)–[0054](docs/adr/0054-alvos-de-ataque-no-cosave.md).
### IA (`settings.json` → `ai`) e diplomacia (`→ diplomacy`, `Factions/*.json`)
A cada `tickHours` horas de jogo as facções de IA atacam vizinhos de facções em guerra com elas (ou neutros, com
`expandIntoNeutral`), pagando a força com o ouro do saldo (`attackBase`, `goldPerStrength`, `min/maxGoldPerAttack`,
`minAdvantage`, `cooldownHours`, `maxActiveAttacks`...). Defesa por tipo em `defenseByType`. Exclusões:
`respectConquestExclusions`, `skipCivilWarForts`. Relações padrão em `Factions/*.json` (`relations`,
`defaultRelation`, `playerRelation`), donos iniciais (`start`) e `aggression` por facção; `diplomacy` tem
`truceHours`, `peaceAcceptRatio`, `allianceAcceptRatio`, `allianceNeedsCommonEnemy`, `warOnConquest`.
Patches `Compat/*.json` (formato em [patches/README.md](patches/README.md)) somam exclusões. Debug: **NumPad7**
(`aiHotkey`, DIK 71) força a avaliação e lista os ataques; **NumPad8** (`diplomacyHotkey`, DIK 72) alterna a sua
relação com o dono do local atual. ADRs [0047](docs/adr/0047-ia-estrategica-ataques-numericos.md)–[0050](docs/adr/0050-loader-compat-e-exclusoes-da-ia.md).
### Lei e prisioneiros (`settings.json` → `law`)
Prisioneiros de guerra: `Law::Prisoners(facção)`, contagem agregada sem ator, ganha por combates da IA (M16,
`CaptureFromCombat`) ou pelo hotkey de debug. `captureFraction` dos derrotados vira prisioneiro, até a
`Capacity` (`baseCapacityPerTerritory` + melhoria `Upgrades.json -> "prison"`). Ações: `Ransom` (resgate, custa
`ransomCostPerPrisoner` de ouro), `Execute` (execução, abala a estabilidade da própria facção, M26). Facção de
crime vanilla do Hold (bounty): `touchCrimeFaction` (padrão **false**, compatibilidade ADR 0005) + `resetBounty`
zeram a recompensa do jogador ao trocar o dono de um território. Debug: **F1** (`lawCaptureHotkey`, DIK 59)
simula capturas; **F2** (`lawRansomHotkey`, DIK 60) resgata; **F12** (`lawExecuteHotkey`, DIK 88) executa o 1º
prisioneiro da sua facção. ADRs [0060](docs/adr/0060-lei-prisioneiros-de-guerra.md)–[0061](docs/adr/0061-facao-de-crime-e-prisao-como-melhoria.md).
Nenhuma destas usa NumPad0: a tecla é mantida livre para o uso futuro já mencionado neste README ("debug de
API"), mesmo que ainda não implementado.
### Ordem pública e rebeliões (`settings.json` → `stability`)
Estabilidade (0–100) por território: `baseline` × doutrina/governador, menos penalidade de guarnição
insuficiente para a população (`popToStrength`, `garrisonPenalty`) e de déficit de comida ou ouro
(`deficitPenalty`). Abaixo de `rebellionThreshold` por `graceHours` horas seguidas, `chancePerHour` por hora de
rebelião: vira a facção `rebels` (`rebelFaction: true`, padrão) ou neutro. Aviso na tela ao ficar instável e ao
rebelar; camada `"stability"` no mapa tático. Debug: **NumPad.** (`stabilityHotkey`, DIK 83) mostra a
estabilidade do território atual e força uma checagem de rebelião imediata. ADRs
[0062](docs/adr/0062-modificador-de-estabilidade.md)–[0063](docs/adr/0063-rebelioes-faccao-rebels-sem-cosave.md).

### Governo local / jarl (`settings.json` → `jarl`, ADR 0056)
Ao conquistar a capital de uma província (cidade de maior população, `Realm::Population`; sem cidade, o
território de maior população), oferece (MessageBox) substituir o governo local por um governador seu —
reaproveita `Government::AssignGovernor` (M18) para a maioria/bônus, mais um NPC próprio (não-único, FaceGen
pronto, outfit pelo DLL) perto do marcador da cidade. O jarl vanilla **nunca é tocado** (ADR 0005). Ativar o NPC
mostra o status da província (população) num MessageBox só-texto (sem dublagem, ADR 0055). Settings:
`askBeforeAssign`, `actor`/`outfit` (`"auto"` ou `"Plugin.esp|0xID"`), `autoHint`, `offset`. Cosave `JARL` v1.
Debug: **NumPad-** (`jarlHotkey`, DIK 74) força a oferta na capital da província atual.

### Domínio de Skyrim (`settings.json` → `realm.dominationThreshold`, ADR 0057)
Ao controlar a maioria de `dominationThreshold` (padrão 1.0 = todas) das províncias existentes, a facção do
jogador recebe o marco permanente "Dominador(a) de Skyrim" e um aviso único na tela — sem travar nada, o mod
continua normalmente. Avaliado dentro do mesmo `Realm::Reevaluate` que já reavalia títulos (troca de dono + tick
estratégico), nunca por polling. Cosave `DOMN` v1. Consulta: `Realm::HasDominated`, native Papyrus
`AgeOfSkyrim.HasDominated`, página "Reino" do MCM.

### Exército (`Troops.json` + `settings.json` → `army`) e batalhas em tempo real (`→ battle`)
Recrute soldados num território seu (`Army::Recruit`): consome ouro, equipamento do baú da sede e população
(`Realm::Population::Draft`); depois de `trainHours` eles entram no exército ABSTRATO do território (contagem
por tipo, oficiais, escolta). Tropas de campo em `Troops.json` (mesmo formato de `Garrison.json`: `actor`,
`autoArmorHint`, `outfit`, + `strength`, `goldCost`, `equipCategory`/`equipAmount`, `trainHours`, `upkeep`).
Marcha só entre vizinhos (`army.hoursPer10000Units`, `min/maxTravelHours`); manutenção e deserção por déficit
como a guarnição (`army.desertPerHour`, `deficitGraceHours`, `aiUpkeep`); `army.maxOfficers`/`officerBonusEach`
(bônus simples, sem NPC); `army.maxEscort` (teto global de soldados em escolta; seguimento físico é TODO).
Quando um ataque da IA chega ao seu território, ou dois exércitos hostis se encontram, com você por perto, a
batalha é MATERIALIZADA (`battle.realTime`): atores em lotes por frame (`battle.spawnPerFrame`), teto GLOBAL
próprio `battle.maxActors` (separado da guarnição), reforços conforme morrem, 2-3 posições por lado
(`battle.sideDistance`/`formationSpacing`), facções de combate criadas em runtime (nunca vanilla). Longe de você
(ou `realTime: false`): resolução numérica (reaproveita `Ai::Resolve`/`Army`). Debug: **NumPad/**
(`armyRecruitHotkey`, DIK 181; NumPad0 segue reservado para a API) recruta no território atual; **NumPad\***
(`armyListHotkey`, DIK 55; NumPad. já é `stabilityHotkey`, M26) lista os seus exércitos; **NumPad+**
(`battleTestHotkey`, DIK 78) força uma batalha de teste perto de você. ADRs
[0064](docs/adr/0064-exercito-abstrato-recrutamento.md) e [0065](docs/adr/0065-batalhas-tempo-real-faccoes-proprias.md).

## Estrutura

| Arquivo | Responsabilidade |
|---|---|
| `src/Core/Ids.h` | IDs por hash FNV-1a (estáveis entre versões do JSON) |
| `src/Core/Config.*` | Leitura dos JSONs, validação, resolução de `Plugin.esp\|0xID`; territórios dinâmicos (claims) |
| `src/Core/State.*` | Facção dona de cada território + persistência no cosave (registro `TERR` v2, migra v1) |
| `src/Core/Factions.*` | Facções pré-definidas (`Factions/*.json`) + do jogador (registro `FACT` v1); dono → estilo |
| `src/Core/I18n.*` | Textos para o jogador (en / pt-br), fallback para inglês, notificações na tela |
| `src/Core/Timer.h` | Tempo das operações no log (`[tempo] ...`) |
| `src/World/Discovery.*` | Territórios descobertos dos `BGSLocation` (tipo, província, posição, vizinhos) |
| `src/World/Placement.*` | Objetos criados em runtime + refs no cosave (registro `PLAC` v1) |
| `src/Debug/SelfTest.*` | Autoteste F11 e checagem de persistência (registro `TEST` v1) |
| `src/Stock/Stock.*` | Estoque por categoria (`Resources.json`), recontagem ao fechar baú observado, debug F8 |
| `src/World/Claims.*` | Free Claim: raycast + alinhamento à normal, limites, registro `CLAM` v2, claim = território dinâmico, debug F3 |
| `src/World/Conquest.*` | Conquista por local limpo (`LocationCleared`), facção padrão, exclusões, ganchos, debug F4 |
| `src/World/Seat.*` | Sede: candidatas (`Seats/*.json`), pergunta, baú + mesa em runtime, mudança, registro `SEAT` v1 |
| `src/UI/Prompt.*` | MessageBox vanilla com botões e callback (sem ESP/Papyrus) |
| `src/UI/WarMap.*` | Mapa tático (M11): snapshot JSON, camadas por provedor (`AddProvider`), ações (rota/alvo), registro `WMAP` v1 |
| `src/UI/WarMapPrisma.cpp`, `PrismaApi.h` | View do PrismaUI (opcional, detectado em runtime) — interface mínima própria |
| `src/UI/WarMapMenus.cpp` | Fallback sem PrismaUI: menus vanilla paginados com as mesmas ações |
| `data/PrismaUI/views/AgeOfSkyrim/` | View HTML/JS/CSS do mapa (SVG procedural); `index.html?sample` abre com dados de exemplo |
| `src/Sim/Economy.*` | Economia (M12): tick por hora por evento, produção por tipo/facção (`Economy.json`), depósito (`SetDepot`), consumo/déficit, registro `ECON` v1, debug F6 |
| `src/Sim/Routes.*` | Rotas de comércio (M13): renda por distância/tipo, corte por inimigo, `CanTrade`, registro `ROUT` v1, debug F7 |
| `src/Realm/Realm.*` | Reino (M18/M19/M22): `OnHoursPassed`, `OnTerritoryOwnerChanged`, estatísticas, debug NumPad1–4 |
| `src/Realm/Modifiers.*` | Modificadores agregados por facção/território (produção, impostos, recrutamento, defesa...) |
| `src/Realm/Data.*` | `Doctrines/*.json`, `Population/*.json`, `Titles/*.json` |
| `src/Realm/Government.*` | Doutrina, cargos, governadores, relógio do reino (registro `GOVT` v1) |
| `src/Realm/Population.*` | População por território, crescimento/fome/guerra/alistamento (registro `POPL` v1) |
| `src/Realm/Titles.*` | Títulos por marcos e desbloqueios (registro `TITL` v1) |
| `src/Sim/Garrison.*` | Guarnição (M14): números por território, spawn em lotes perto do jogador com teto global, outfit pelo DLL, manutenção/deserção, registro `GARR` v1, debug NumPad5 |
| `src/Sim/Upgrades.*` | Melhorias (M15): `Upgrades.json`, custo no baú da sede, obra no tick, status por território, visual por nível, registro `UPGR` v1, debug NumPad6 |
| `include/AgeOfSkyrimAPI.h` | API pública C++ versionada (interface, eventos, pedido por mensagem SKSE) |
| `src/Api/ModApi.*` | Implementação da interface, mensagem 'AOSI', export `AoS_GetInterface`, ganchos para M6/M12/M13/M18 |
| `src/Api/Events.*` | Registro central de eventos: callbacks C++ + ModEvents `AoS_*`, sempre na thread principal |
| `src/Papyrus/Api.*` | Natives do `AgeOfSkyrim.psc` (MCM e mods só-script) |
| `data/Scripts/Source/*.psc` | `AgeOfSkyrim.psc` (natives) e `AgeOfSkyrimMCM.psc` (MCM do SkyUI) |
| `tools/papyrus/` | Stubs de compilação próprios + flags (Caprica no CI); não vão no pacote |
| `tools/build_esp.py` | Gera o `AgeOfSkyrim.esp` (ESL, quest do MCM) — testado em `tests/test_build_esp.py` |
| `tools/gen_skyui_translations.py` | `Interface/Translations/AgeOfSkyrim_*.txt` a partir dos JSON de tradução |
| `src/Sim/Ai.*` | IA (M16): avaliação estratégica no tick, ataques numéricos (registro `WARS` v1), provedor de defesa, sede ocupada, debug NumPad7 |
| `src/Sim/Diplomacy.*` | Diplomacia (M17): guerra/paz/aliança/trégua por par, padrões em `Factions/*.json`, registro `DIPL` v1, debug NumPad8 |
| `src/Realm/Jarl.*` | Governo local (M24): capital por população, NPC não-único perto do marcador, ativação com status, registro `JARL` v1, debug NumPad- |
| `src/Realm/Domination.*` | Domínio de Skyrim (M27): marco permanente por fração de províncias, registro `DOMN` v1 |
| `src/Sim/Law.*` | Lei e prisioneiros (M25): `Prisoners`/`Capacity`/`CaptureFromCombat`/`Ransom`/`Execute`, facção de crime do Hold, registro `LAWS` v1, debug F1/F2/F12 |
| `src/Sim/Stability.*` | Ordem pública e rebeliões (M26): estabilidade por território, `Modifier::Stability`, rebeliões (`SetTerritoryOwner`), camada no mapa, sem cosave (ADR 0063), debug NumPad. (decimal) |
| `src/Sim/Troops.*` | Tropas de campo (M20/M21): `Troops.json`, mesma escolha de `ActorBase`/outfit que a guarnição, dados de custo/treino/manutenção |
| `src/Sim/Army.*` | Exército abstrato (M20): recrutamento, treino, marcha, oficiais, escolta, deserção, registro `ARMY` v1, debug NumPad//NumPad* |
| `src/Sim/Battle.*` | Batalhas em tempo real (M21): materialização perto do jogador, teto global próprio, lotes por frame, facções de combate próprias, registro `BATL` v1 (só limpeza), debug NumPad+ |
| `src/Banner/BannerController.*` | Único ponto que chama `Enable()/Disable()`; `SetTerritoryOwner()` |
| `src/Events/Events.*` | Retentativa ao carregar células; hotkeys de debug (F1/F2/F3/F4/F6–F8/F10–F12, NumPad0–9/./-/+/*) |
| `src/Main.cpp` | Entrada SKSE, log, mensagens; um laço lê o cosave e despacha cada registro ao seu módulo |

## Pendências conhecidas da Fase 1

- [x] Compilar (CI)
- [ ] Validar no jogo — roteiro M2 em [TESTES.md](TESTES.md) (ver "Pontos a verificar" abaixo)
- [x] Criação de facção (nome, estilo, cor) com persistência no cosave (M3; UI vem no MCM)
- [x] *Free Claim*: raycast com alinhamento à normal do terreno (debug F3; vira território dinâmico — M6)
- [x] Ligar a conquista real (captura de local) a `SetTerritoryOwner` (M5, aguardando teste)

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
