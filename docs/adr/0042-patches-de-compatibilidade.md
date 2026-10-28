# 0042. Patches de compatibilidade como dados (`Compat/*.json`) e lista de mods de guerra civil

- **Status:** Aceita (leitura no DLL pendente)
- **Data:** 2026-10-28
- **Origem:** Técnica (M28)

## Contexto
O roadmap pede patches JSON opcionais para mods de locais (JK's, Lux, Open Cities) e uma lista de exclusão para
mods de guerra civil (lição `ISSUE-QST-03/04` do comparativo: não brigar com a Guerra Civil). Hoje o DLL lê:
- `Territories/*.json` (vários arquivos, ordem alfabética): estilos e territórios; um território com `"location"`
  sobrescreve só o que declarar do local descoberto (ADR 0016) — **patches de locais já funcionam por aqui**;
- `settings.json → discovery.exclude` (`Plugin.esp|0xID`), num arquivo **único**: um patch não consegue somar
  exclusões sem sobrescrever o `settings.json` do usuário;
- o bloco `settings.json → conquest` com exclusões está sendo criado em paralelo (M5).

Não há como conferir aqui FormIDs nem nomes de plugin de mods de terceiros (sem acesso ao Nexus/xEdit).

## Decisão
- Patch = pasta `patches/<nome>/` espelhando `Data/` (instalada pelo FOMOD, ADR 0041) com:
  - `SKSE/Plugins/AgeOfSkyrim/Territories/<nome>.json` para territórios/sedes (formato atual);
  - `SKSE/Plugins/AgeOfSkyrim/Compat/<nome>.json` para exclusões condicionais, formato em `patches/README.md`:
    `name`, `whenLoaded` (ativo se **qualquer** plugin listado estiver carregado; vazio = sempre), `plugins`
    (só documentação: nome no Nexus, tipo, `verified`), e blocos `discovery` / `conquest` com **as mesmas chaves
    do `settings.json`**. Listas são somadas (união) às do `settings.json`; escalares só valem se o usuário não os
    definiu no `settings.json`.
- Chaves de `conquest` propostas para o patch: `exclude` (`Plugin.esp|0xID`), `excludeFromPlugins` (todo
  `BGSLocation` cujo arquivo de origem é um desses plugins) e `skipCivilWarForts` (fortes `LocTypeMilitaryFort`
  disputados pela Guerra Civil vanilla não são conquistáveis). Se o bloco `conquest` da M5 usar outros nomes, o
  loader segue os da M5 e este formato é ajustado junto.
- **Sem FormID de terceiros.** O patch `civil-war-mods` usa só nomes de plugin, todos `"verified": false`:

  | Plugin | Mod | Tipo | Estado |
  |---|---|---|---|
  | `Civil War Overhaul.esp` | Civil War Overhaul (CWO) | guerra civil | **a verificar** |
  | `Warzones - Civil Unrest.esp` | Warzones - Civil Unrest | guerra civil | **a verificar** |
  | `Immersive Patrols II.esp` | Immersive Patrols | patrulhas (só ativa o patch) | **a verificar** |

  Nome errado não quebra nada: o plugin não é encontrado e o patch fica inativo (aviso no log).
- Patches de JK's/Lux/Open Cities ficam para quando houver sede (M6) e conferência no jogo: precisam de
  coordenadas de baú/mesa medidas, não de memória.

## TODO no loader (`src/Core/Config.cpp`, fora do escopo desta mudança)
1. Depois de `LoadSettings`, ler `Compat/*.json` em ordem alfabética (mesmo `ReadJson`, com comentários).
2. `whenLoaded`: `RE::TESDataHandler::GetSingleton()->LookupLoadedModByName(nome)` ou
   `LookupLoadedLightModByName(nome)` (ESL) — ambos existem em `RE/T/TESDataHandler.h`; nenhum carregado →
   `logger::info` e pula.
   Ler em `kDataLoaded` (o `Config::Load` já roda lá por causa da descoberta).
3. `discovery.exclude`: inserir em `discovery_.exclude` com a mesma normalização de `FormRef` já usada.
4. `conquest.exclude` / `excludeFromPlugins` / `skipCivilWarForts`: somar ao bloco `conquest` da M5;
   `excludeFromPlugins` resolvido uma vez para um conjunto de índices de arquivo (`TESFile`) e checado em O(1) na
   conquista (`BGSLocation::GetFile(0)`); `skipCivilWarForts` precisa da detecção da Guerra Civil vanilla
   (sem FormIDs de memória: resolver as quests/locais pelo EditorID em runtime e conferir no jogo).
5. Log: `Compat: <nome> ativo (N exclusões)` / `inativo (nenhum plugin de whenLoaded carregado)`.
6. Opcional: `territories` dentro do `Compat/*.json`, tratado como mais um arquivo de `Territories/` só quando o
   patch está ativo (território condicional ao mod existir).

## Consequências
- Patches são só dados: qualquer mod pode trazer o próprio `Compat/<mod>.json` sem esperar um release nosso.
- Até o TODO ser feito, instalar o patch `civil-war-mods` não muda nada no jogo (arquivo ignorado pelo DLL).
- A lista de plugins precisa de uma conferência humana no Nexus antes do release (marcar `verified: true`).
