# Age of Skyrim — Roadmap

Status: ✅ concluída · 🔄 em andamento · ⬜ a fazer · 🎮 precisa de teste no jogo (roteiro em [TESTES.md](../TESTES.md)).
As decisões por trás deste plano estão em [docs/adr](adr/README.md).

## Decisões já tomadas
- Conquista = **local limpo** ([ADR 0009](adr/0009-conquista-por-local-limpo.md)).
- UI = **MCM primeiro, PrismaUI depois**, sobre a mesma API nativa ([ADR 0010](adr/0010-ui-mcm-depois-prismaui.md)).
- **Sede** entre candidatas, **estoque** com itens reais por categoria, **mesa de guerra** com mapa tático
  ([ADR 0011](adr/0011-sede-entre-candidatas.md), [0012](adr/0012-estoque-itens-reais.md), [0013](adr/0013-mesa-de-guerra-mapa-tatico.md)).
- **Melhorias** de territórios com custo + tempo de obra e visual por nível ([ADR 0020](adr/0020-melhorias-de-territorios.md)).
- Tudo traduzido em **inglês e PT-BR** ([ADR 0014](adr/0014-traducao-en-ptbr.md)).
- Eu implemento tudo; o usuário só testa, com roteiro curto + autoteste ([ADR 0008](adr/0008-autoteste-e-fluxo-de-testes.md)).

## Princípios (valem para TODAS as missões)

**Performance**
- 100% orientado a eventos: nada de polling por frame, nada de `RegisterForUpdate`/loops em Papyrus. Ticks
  estratégicos (economia, guerra) rodam no máximo 1×/hora de jogo, com lotes pequenos por tick.
- Papyrus só como casca fina de UI (MCM); toda lógica no DLL. Natives Papyrus nunca bloqueiam (sem I/O, sem lock longo).
- Caminhos quentes (sinks de input/célula) com custo ~zero quando não há trabalho (padrão já usado em `RetryPending`).
- Lookups O(1) pré-computados no `Config::Load` (ex.: `BGSLocation` → território, como `index_`); nada de varrer listas em evento.
- Mexer no mundo só quando o estado difere (padrão de `SetEnabled`); spawns limitados e com teto configurável.
- Cosave compacto (só IDs/números) e com tamanho medido no log; refs persistentes no ESP no mínimo necessário.
- Cada missão com timing no log (`spdlog` + `std::chrono`) das operações novas; meta: < 1 ms por evento.

**Integração com outros mods**
- **Zero edição de registros vanilla** no `AgeOfSkyrim.esp` (nem células, nem locais, nem NPCs). Bandeiras são refs
  novas; ligação a locais vanilla é feita pelo JSON (`Skyrim.esm|0xID`) → sem conflito com Lux, JK's, Open Cities, etc.
- ESP como **ESL-flagged (ESPFE)** sempre que possível, para não consumir slot de load order.
- Tudo data-driven e extensível por outros mods: qualquer mod pode soltar `Territories/*.json` / `Factions/*.json`
  (já é assim) — e o loader tolera plugins ausentes (território ignorado com aviso, sem crash).
- **API pública desde cedo** (M9, não no fim): mensagens SKSE (`OnTerritoryChanged`, consulta de dono) + header C++ +
  natives Papyrus, para mods de guerra civil/patrulha/economia se integrarem.
- Dependências sempre **opcionais** e detectadas em runtime (SkyUI, PrismaUI, PO3 Tweaks...): sem elas, o mod degrada,
  não quebra. Hard deps só SKSE + Address Library.
- Respeitar outros mods de guerra/facção: opção de não reagir em locais já controlados pela Guerra Civil vanilla
  ou por mods conhecidos (lista de exclusão no JSON).
- **Tradução:** nenhum texto de jogador no código; toda chave em `en.json` e `pt-br.json` (CI confere).
- Seguir convenções do ecossistema (formato `Plugin.esp|0xID` compatível com SPID/KID/BOS) e documentar no README.

Regras para todas as missões:
- Reaproveitar os padrões existentes: IDs por `MakeId` (`src/Core/Ids.h`), registros de cosave com `Tag()` + versão
  (`src/Core/State.cpp`), mudanças no mundo só via `SKSE::GetTaskInterface()->AddTask`, sinks de evento como os de
  `src/Events/Events.cpp`, mudança de dono sempre por `SetTerritoryOwner` (`src/Banner/BannerController.cpp`).
- Novo registro de cosave = nova tag + versão; nunca quebrar `TERR` v1 (subir para v2 com migração, se preciso).
- Toda missão: CI verde (a partir da M1) + atualizar README (estrutura/pendências) + commit + push.
- Missões 🎮 terminam com roteiro de teste (≤ 5 passos) em `TESTES.md` + autoteste no DLL; as outras são validadas pelo CI.
- Toda decisão nova vira um ADR em `docs/adr/`.

---

## Fase 1 — Fechar o núcleo

### M1. ✅ CI no GitHub Actions (build do DLL)
- `.github/workflows/build.yml` (`windows-2022`, vcpkg recente + baseline fixo, cache em disco), artefato zip no layout do mod.

### M2. 🔄🎮 Primeiro teste no jogo (sem ESP) + autoteste
- Objetos de teste criados em runtime perto do jogador (`World/Placement`, registro `PLAC`), sem Creation Kit.
- F11 = autoteste (visibilidade por dono, PASS/FAIL na tela e no log); ao carregar o save, checagem de persistência (`TEST`).
- Infra de tradução (`Core/I18n`, `Translations/en.json` + `pt-br.json`) e checagem de acentos no HUD.
- Despacho central do cosave em `Main.cpp`; tempo das operações no log.

### M3. 🔄 Facções do jogador (modelo + cosave) — [ADR 0019](adr/0019-modelo-de-faccoes.md)
- `src/Core/Factions.{h,cpp}`: `Faction { Id id; std::string name; Id style; std::uint32_t color; bool isPlayer; }`.
- Registro de cosave `FACT` v1. O dono de território passa a ser **facção** (não estilo): `TERR` v2 com migração
  (v1: o estilo gravado vira uma facção pré-definida de mesmo nome).
- Facções pré-definidas no JSON (`factions` em `Territories/*.json` ou novo `Factions.json`): Império, Stormcloaks.
- `BannerController` resolve facção → estilo para escolher as bandeiras.
- Hotkey de debug passa a ciclar entre facções.

### M4. 🔄🎮 Territórios do jogo (auto-descoberta) + tipos de local
- `World/Discovery`: territórios criados a partir dos `BGSLocation` pelas keywords `LocType*` (fort/camp/village/city),
  incluindo os de outros mods ([ADR 0016](adr/0016-territorios-auto-descobertos.md)).
- Por território: `type`, `province` (Hold), `position` (marcador do mapa), `neighbors` (k mais próximos), lookup
  O(1) local → território (para a M5).
- JSON com `"location"` sobrescreve/complementa; `settings.json` → `discovery` (tipos, exclusões, vizinhos).
- Verificação: resumo PASS/FAIL no log + `SKSE/AgeOfSkyrim_territories.json` (debug).

### M5. ✅🎮 Conquista por "local limpo" — implementada, aguardando teste no jogo ([ADR 0022](adr/0022-conquista-implementacao.md))
- ✅ `World/Conquest`: sink de `LocationCleared::Event` (a CommonLib não tem `TESLocationClearedEvent`; o evento não
  traz o local) → cadeia de locais do jogador, primeiro local limpo com território (O(1)) → `SetTerritoryOwner`.
- ✅ Facção padrão criada na 1ª conquista se o jogador não tem uma (nome traduzido, estilo/cor de settings).
- ✅ `settings.json → conquest`: `enabled`, `requirePlayerInLocation`, `onlyFromNeutral`, `exclude`, `excludePlugins`,
  `defaultStyle`, `defaultColor`. Notificação + log; re-spawn não reverte.
- ✅ Ganchos `Conquest::AddListener` / `ConquerForPlayer` (sede usa; M9 expõe). Debug F4 (`conquestHotkey`) simula.

### M6. ✅🎮 Sede da facção (capital) — implementada, aguardando teste no jogo ([ADR 0023](adr/0023-sede-implementacao.md), [0025](adr/0025-objetos-base-da-sede.md))
- ✅ Candidatas em `Seats/*.json` (extensível): regra `auto` (N fortes/torres por província, mais próximos da cidade)
  + lista explícita (`territory`/`location`, offsets) + `exclude`.
- ✅ Ao conquistar uma candidata: MessageBox vanilla "Fundar sede aqui?" / "Mudar a sede?" (`UI/Prompt`) → `SEAT` v1.
- ✅ Baú (CONT vanilla sem respawn, escolhido em runtime) e mesa de guerra (ACTI vanilla) criados com
  `Placement::PlaceAt` relativos ao jogador (sem coordenadas curadas ainda); FormIDs no cosave.
- ✅ Mudar de sede move o inventário do baú antigo (com ExtraDataList) e apaga os objetos antigos.
- ✅ Gancho M11: ativar a mesa (sink `TESActivateEvent` filtrado pelo ref) → `Seat::OnWarTableActivated()` (só notifica).
- ⬜ Base objects próprios no `AgeOfSkyrim.esp` (ESL) e coordenadas curadas por local (M18).

### M7. 🔄 🎮 Estoque por categoria — núcleo pronto (na main), ligação ao baú na M6
- ✅ `Stock/`: `Resources.json` com categorias localizadas (ouro, comida, metais, materiais, alquimia, valiosos,
  equipamento) e regras por form, keyword vanilla e tipo de form; precedência e custo em [ADR 0017](adr/0017-estoque-classificacao-por-keyword.md).
- ✅ `CountByCategory(container)`: uma passada em `GetInventoryCounts()`, classificação memorizada por objeto base.
- ✅ Sink de `ContainerMenu`: reconta ao fechar só containers observados (custo zero sem observados).
- ✅ Debug F8: inventário do jogador por categoria na tela + log (EditorID de cada keyword no log).
- ✅ M6: `Stock::Observe(baú da sede)` + recontagem ao fundar/carregar; `Seat::StockTotals()`; F8 mostra o estoque da sede.
- ✅ M11: totais no mapa. ⬜ M9: evento `OnStockChanged`.

### M8. 🔄 🎮 Free Claim (estandarte em local genérico) — núcleo pronto (na main)
- ✅ Debug F3 (era F9, que é o quickload do jogo): raycast vertical (`bhkWorld::PickObject`) à frente do jogador → ponto + normal do terreno;
  estandarte criado com `Placement::PlaceAt`, alinhado à normal e sem física ([ADR 0018](adr/0018-free-claim-raycast.md)).
- ✅ Cosave `CLAM` v1 (serial, Id estável, worldspace, posição, FormID do ref); limites em `settings.json → claim`
  (distância mínima, máximo, inclinação máxima, sem interiores).
- ✅ Claim vira território dinâmico (tipo `claim`) da facção do jogador: `Config::AddDynamic`, `CLAM` v2, ordem
  `FACT → CLAM → TERR` no cosave ([ADR 0024](adr/0024-claims-como-territorios.md)).
- ⬜ Item/power "Fincar Estandarte" no lugar da hotkey.

## Fase 2 — Interface e gestão

### M9. ✅ Camada de API (Papyrus + SKSE para outros mods) — implementada, aguardando teste no jogo ([ADR 0032](adr/0032-api-publica-para-outros-mods.md), [0035](adr/0035-papyrus-compilado-no-ci-caprica.md))
- ✅ `include/AgeOfSkyrimAPI.h` (`IAgeOfSkyrim` v1, ABI só com POD) pedido por mensagem SKSE 'AOSI' ou export
  `AoS_GetInterface`; `src/Api/ModApi` implementa e expõe ganchos (`Set*Provider`) para sede/tesouro/rotas/governo.
- ✅ `src/Api/Events`: `Emit` de qualquer thread → thread principal → callbacks C++ + ModEvent `AoS_*`. Emissores hoje:
  StateReady, TerritoryChanged, FactionCreated/Changed, ClaimCreated, StockChanged; demais reservados (M6/M12/M13/M18/M19/M22).
- ✅ `src/Papyrus/Api` + `data/Scripts/Source/AgeOfSkyrim.psc` (natives por chave de texto); compilado no CI com Caprica
  e stubs próprios (`tools/papyrus/`). `settings.json` → `api` (`papyrusEvents`, `allowOwnerChanges`).

Plano original:
- `src/Api/ModApi.{h,cpp}` + `include/AgeOfSkyrimAPI.h` público: interface versionada via
  `SKSE::GetMessagingInterface()` (outros DLLs pedem a interface em `kPostLoad`), eventos `OnTerritoryChanged`,
  `OnFactionCreated`, `OnSeatFounded`, `OnStockChanged`.
- Evento Papyrus equivalente (`ModEvent "AoS_TerritoryChanged"` etc.) para mods só-script.
- `src/Papyrus/Api.{h,cpp}`: natives em `AgeOfSkyrim.psc` — facção, territórios/donos, sede, estoque, setar dono (debug).
- `data/Scripts/Source/AgeOfSkyrim.psc` + script compilado no CI (Caprica, fora do CK).
- Base do MCM (M10) **e** do mapa PrismaUI (M11).

### M10. ✅ 🎮 MCM (SkyUI) — implementada, aguardando teste no jogo ([ADR 0033](adr/0033-mcm-skyui-casca-fina.md), [0034](adr/0034-esp-gerado-sem-creation-kit.md))
- ✅ `AgeOfSkyrimMCM.psc`: Facção (criar/renomear/estilo/cor), Territórios (40 por página + dono; menu de dono com debug),
  Sede e Estoque ("sem sede" até a M6 + inventário por categoria), Economia e Governo (em breve), Depuração.
- ✅ Textos pelo DLL (`AgeOfSkyrim.Tr`, idioma do `settings.json`); tokens do SkyUI só para nome do mod e "DLL ausente",
  gerados por `tools/gen_skyui_translations.py`; `check_translations.py` confere as chaves usadas nos `.psc`.
- ✅ `AgeOfSkyrim.esp` (ESL) gerado sem CK por `tools/build_esp.py` (quest + VMAD + alias do jogador), validado por
  `tests/test_build_esp.py`. SkyUI opcional (caixa de texto exige SkyUI 5.x).

Plano original:
- `AgeOfSkyrimMCM.psc` (quest + `SKI_ConfigBase`): "Facção" (criar/renomear/estilo/cor), "Territórios" (lista + dono),
  "Sede & Estoque" (totais por categoria), "Debug".
- Quest do MCM adicionada ao ESP (passo a passo do CK).

### M11. ✅ 🎮 Mesa de guerra + mapa tático (PrismaUI) — implementada, aguardando teste no jogo — [ADR 0051](adr/0051-prismaui-opcional-interface-minima.md)–[0054](adr/0054-alvos-de-ataque-no-cosave.md)
- Ativar a mesa de guerra da sede (evento de ativação filtrado pelo ref da sede) abre a view PrismaUI.
- Mapa de Skyrim com: territórios por tipo e cor da facção dona, províncias, vizinhança, estoque da sede,
  painel de cada local (tipo, dono, guarnição, renda, rotas que passam por ele).
- Ações no mapa: marcar alvos de ataque, traçar rotas de comércio (M13), ver ameaças (M16).
- PrismaUI é dependência opcional, detectada em runtime: sem ela, a mesa abre um menu de listas vanilla
  (MessageBox / ListMenu) com as mesmas ações essenciais.
- Dados enviados para a view só ao abrir e quando mudam (sem polling JS ↔ C++).
- Feito: `src/UI/WarMap*` — `Seat::OnWarTableActivated` abre o mapa; PrismaUI detectado em runtime por
  `GetModuleHandle`/`RequestPluginAPI` (interface mínima própria em `UI/PrismaApi.h`), view criada na 1ª abertura.
- Feito: view `data/PrismaUI/views/AgeOfSkyrim/` (SVG procedural: ícone por tipo, cor do dono, províncias por fecho
  convexo, vizinhança, rotas ativas/cortadas, painel do território com estoque, rotas e camadas; criar/remover rota,
  marcar alvo; zoom/arrasto; Esc fecha). Textos `warmap.*` en/pt-br enviados no snapshot.
- Feito: snapshot JSON (`WarMap::BuildSnapshot`, ScopedTimer) com schema em `docs/warmap-schema.json`, teste
  `tests/test_warmap_schema.py`, dados de exemplo `tools/warmap_sample.py` + `index.html?sample` fora do jogo.
- Feito: camadas extras por provedor (`WarMap::AddProvider("garrison", ...)`) e `NotifyChanged()` para M14/M16/M9.
- Feito: alvos de ataque no cosave `WMAP` v1 (`WarMap::Targets()` para a M16); fallback com MessageBox vanilla
  (listas paginadas próximos/meus/alvos/rotas, painel, criar rota com 2 escolhidos). Debug NumPad9 abre o mapa.
- ⬜ M14: camada de guarnição; M12: renda por território no painel; M16: ameaças.

### M12. ✅ Economia — implementada, aguardando teste no jogo — [ADR 0026](adr/0026-tick-por-hora-orientado-a-evento.md), [ADR 0027](adr/0027-economia-producao-deposito-consumo.md)
- Tick por hora de jogo (lote, < 5 ms): territórios geram recursos por tipo (vila → comida, mina → metais, cidade → ouro),
  depositados como itens no baú da sede (respeitando a classificação da M7).
- Consumo: guarnições comem comida e recebem ouro; falta de estoque → moral/desserção (gancho para M14).
- Registro de cosave `ECON` v1 (último tick, saldos pendentes).
- Feito: `src/Sim/Economy.*`. Tick disparado por eventos que o jogo já emite (fechar menu, célula carregada, fim de
  sono/espera) comparando `Calendar::GetHoursPassed`; horas atrasadas em lote, limitadas por `maxCatchUpHours`.
- Feito: `Economy.json` + `Economy/*.json` (recursos = itens reais por categoria, produção por tipo de território,
  multiplicador por facção). Jogador → itens no depósito (`Economy::SetDepot`, a ligar na sede M6; sem depósito
  acumula pendente); IA → saldo numérico (`Balance`, para M16).
- Feito: `RegisterUpkeep` (custo por hora, para M14) e déficit consultável (`InDeficit`/`Deficit`/`DeficitHours`).
- Debug F6 (`debug.economyHotkey`): força `economyHours` horas e mostra o resumo.

### M13. ✅ Rotas de comércio — implementada, aguardando teste no jogo — [ADR 0028](adr/0028-rotas-de-comercio.md)
- Rotas entre territórios da facção (ou com facções em paz, M17) traçadas no mapa; cosave `ROUT` v1.
- Rendem ouro por tick conforme distância/tipo dos pontos; rota passando por território inimigo pode ser cortada.
- Simulação fora da tela (sem spawn de caravanas no MVP; caravanas visíveis ficam como extensão futura).
- Feito: `src/Sim/Routes.*` com `Create/Remove/List/StatusOf` (para M10/M11), `CanTrade` (hoje: mesma facção),
  limite `routes.maxRoutes`; renda = base + distância × média do multiplicador por tipo; corte se território
  inimigo a menos de `blockRadius` do segmento entre as pontas.
- Debug F7 (`debug.routeHotkey`): rota entre os 2 territórios mais próximos do jogador com o mesmo dono.
- Falta (outras missões): traçar no mapa (M11), menu no MCM (M10), diplomacia em `CanTrade` (M17).

### M14. ✅ 🎮 Guarnição — implementada, aguardando teste no jogo — [ADR 0043](adr/0043-guarnicao-numerica-materializada.md), [0044](adr/0044-tropas-bases-vanilla-outfit-faccoes.md)
- Ao conquistar, spawn de N guardas da facção (leveled list por estilo, definida no ESP) no marcador do território.
- Guardas registrados no cosave (`GARR` v1); limpeza ao perder o território. Teto global de NPCs configurável.
- Manutenção paga com o estoque da sede (M12).
- Feito: `src/Sim/Garrison.*`. Guarnição **numérica** por território (`initial`/`max` por tipo em `Garrison.json`,
  teto + melhorias M15), cosave `GARR` v1; troca de dono (listener de conquista + reconciliação) zera para a
  guarnição inicial do novo dono.
- Feito: atores só perto do jogador (âncora = `worldLocMarker` ou bandeira em célula carregada, `spawnRadius`),
  lotes de `spawnPerFrame` por frame, teto global `maxActors`; despawn (`Disable`+`SetDelete`) ao descarregar a
  célula; mortes reduzem o número; atores de saves antigos apagados no pós-load.
- Feito: tropas por facção/estilo (`troops`: candidatos FormID/EditorID + escolha automática por armadura do
  `Skyrim.esm`), outfit aplicado e conferido pelo DLL, `PlayerFaction` para soldados do jogador.
- Feito: manutenção por soldado (`RegisterUpkeep`), reposição por hora e deserção após `deficitGraceHours` de déficit.
- API: `Garrison::Size/Max/Strength/Present`, `Set/Add`. Debug NumPad5 (`debug.garrisonHotkey`).

### M15. ✅ 🎮 Melhorias de fortes, torres e outros territórios — implementada, aguardando teste no jogo — [ADR 0020](adr/0020-melhorias-de-territorios.md), [0045](adr/0045-melhorias-implementacao.md)
- `Upgrades.json` (extensível por outros mods): melhorias por tipo de local, cada uma com níveis I → II → III.
  Ex.: muralhas (defesa), quartel (guardas/reposição), torre de vigia (alerta e visão no mapa),
  armazém (capacidade do estoque), mercado (renda/rotas), forja (metais → equipamento da guarnição).
- Custo em **itens reais do baú da sede** (classificação da M7) + **tempo de obra** em horas de jogo; o local fica
  "em obra" (mais vulnerável) até terminar. Obras conferidas no tick por hora já existente (M12), sem custo contínuo.
- **Visual por nível:** objetos extras (barricadas, tendas, braseiros, estandartes) criados em runtime via `Placement`,
  com coordenadas por local curadas em JSON; locais sem coordenadas recebem só os status.
- Status numéricos no DLL (defesa, capacidade, renda, visão, guarnição máx.) lidos por M11/M12/M13/M14/M16;
  cosave `UPGR` v1 (nível por melhoria, obra em andamento + término).
- Iniciar/acompanhar obras pela mesa de guerra (M11) e pelo MCM (M10). Textos en/pt-br.
- Feito: `src/Sim/Upgrades.*` + `Upgrades.json` (muralhas, quartel, torre de vigia, armazém, mercado, forja; I–III),
  custo por categoria do estoque pago do baú da sede, obra em horas no tick da economia (`AddTickListener`,
  [ADR 0046](adr/0046-ganchos-do-tick-da-economia.md)), status em cache (`Upgrades::Stat`), renda via
  `Economy::SetTerritoryMultiplier`, guarnição máx./defesa lidas pela M14, cosave `UPGR` v1.
- Feito: visual por nível ancorado no marcador/bandeira (`visuals`), exemplo na `test_tower`.
- API: `Upgrades::Stat/Level/InConstruction/CurrentConstruction/NextAvailable/Start`. Debug NumPad6
  (`debug.upgradeHotkey`, `debug.upgradeIgnoreCost`).
- Falta (outras missões): obras pela mesa de guerra (M11) e MCM (M10); capacidade do estoque e visão aplicadas.

## Fase 3 — Mundo vivo

### M16. ⬜ Facções de IA e contra-ataques
- Tick estratégico (a cada X horas de jogo): cada facção de IA avalia vizinhos (`neighbors`) e decide atacar,
  considerando guarnição e estoque.
- Ataque fora da tela: resolução por força. Na tela: spawn de grupo atacante + evento de defesa.
- Ameaças e ataques em andamento aparecem no mapa tático.

### M17. ⬜ Diplomacia básica
- Relações entre facções (guerra/paz/aliança) no cosave; afeta IA, rotas de comércio e hostilidade dos guardas.
- Na Fase 4, a IA (M16) e a diplomacia passam a usar exércitos (M20) e batalhas (M21) no lugar da resolução por força simples.

## Fase 4 — Reino e exército ([ADR 0031](adr/0031-exercito-governo-populacao-titulos.md))

Inspiradas no Conquest of Skyrim ([comparativo](COMPARATIVO-CONQUEST-OF-SKYRIM.md)), mas com as lições da seção 2
como requisito: lógica no DLL, teto de atores, sem navmesh nem NPC vanilla com pacotes.

### M18. ✅ Governo e doutrina — núcleo numérico implementado, aguardando teste no jogo ([ADR 0036](adr/0036-governo-doutrina-cargos.md), [0039](adr/0039-reino-modulo-integracao.md))
- ✅ `src/Realm/`: `Doctrines/*.json` (feudal, militar, república, mercantil; extensível, id repetido substitui),
  `Realm::Modifiers::Get/GetFor` (doutrina + cargos + governador + título), 1ª escolha livre, troca com
  `changeCost` (gancho `Government::SetPaymentHandler`) e espera `realm.doctrineCooldownHours` no relógio do reino.
- ✅ Cargos como dados + bônus (`AssignOfficer(facção, cargo, npc)`, vagas por título); governadores por província
  com maioria (bônus regional; saem ao perder a maioria). Cosave `GOVT` v1. Debug F7 resumo / F3 próxima doutrina.
- ⬜ NPCs dos cargos na sede (depois da M6) e escolha na criação da facção (M10).
- Doutrinas data-driven (`Doctrines.json`, extensível): ex. feudal, militar, república, mercantil. Cada uma é
  um conjunto de modificadores numéricos (produção, impostos, custo/velocidade de recrutamento, crescimento da
  população, defesa) lidos por M12/M13/M14/M19/M20. Escolha na fundação da facção, troca com custo e espera.
- Cargos da facção (mordomo → economia, comandante → exército, ferreiro → equipamento): NPCs **nossos**
  (base vanilla, sem aliases/pacotes vanilla) colocados na sede, cada um dá bônus e abre o menu da sua área.
- Governadores por província conquistada (bônus regional). Cosave `GOVT` v1. Textos en/pt-br.

### M19. ✅ População — núcleo numérico implementado, aguardando teste no jogo ([ADR 0037](adr/0037-populacao-numerica.md))
- ✅ `Population/*.json` (inicial por tipo + por território); crescimento logístico/fome em `Realm::OnHoursPassed`
  (gancho de comida `Population::SetFoodProvider`), perda na troca de dono, `Draft(território, n)` para a M20,
  mão de obra no `Production` de `Modifiers::GetFor`. Cosave `POPL` v1 compacto (tamanho no log). Debug F6 simula horas.
- ⬜ M12 chama `OnHoursPassed` e fornece a comida; M10/M11 mostram os números.
- Cada território tem população numérica por tipo (vila/cidade/forte), simulada no tick por hora (M12):
  cresce com comida no estoque, cai com fome, guerra e alistamento.
- Mão de obra multiplica a produção (M12); população é a fonte de recrutas (M20) e de impostos (doutrina M18).
- Sem atores: só números. Cosave `POPL` v1; visível no mapa tático (M11) e no MCM (M10).

### M20. ⬜ 🎮 Exército e recrutamento
- Recrutar soldados nos territórios (consome população, ouro e equipamento do estoque; tempo de treino).
  Tipos de tropa data-driven (`Troops.json`: leveled list/NPC vanilla base com FaceGen pronto, outfit por estilo
  da facção aplicado pelo DLL, custo, força).
- Exércitos = unidades **abstratas** (contagens por tipo, força, moral, posição em território) no cosave `ARMY` v1;
  movem-se entre vizinhos no mapa tático. Só viram atores perto do jogador (M21).
- Oficiais/comandantes de exército; manutenção paga pela economia; falta de comida/ouro → deserção.
- O jogador pode levar um grupo pequeno como escolta (teto configurável).

### M21. ⬜ 🎮 Batalhas em tempo real
- Quando um exército encontra outro (ou ataca um território) com o jogador por perto, a batalha é
  **materializada**: atores criados em lotes por frame nos marcadores do local, **teto global de atores**
  (configurável; o restante entra como reforço quando alguém morre), facções de combate por facção do mod.
- Formações simples (linha, cunha, defesa) por posição de spawn e pacotes nossos; reforços em ondas.
- Resultado da batalha atualiza as contagens do exército abstrato; longe do jogador, a resolução é numérica
  (a mesma da M16). Limpeza dos atores ao fim ou ao sair da área. Desempenho medido no log (meta: sem picos > 5 ms/frame).

### M22. ✅ Títulos e progressão — núcleo implementado, aguardando teste no jogo ([ADR 0038](adr/0038-titulos-progressao.md))
- ✅ `Titles/*.json` (Chefe de Bando → Senhor → Jarl → Rei/Rainha Supremo(a)) com marcos (territórios, províncias
  com maioria, cidades, população), avaliação só em eventos, títulos só sobem, anúncio traduzido no gênero do jogador,
  `unlocks` numéricos (`officers`, `governors`, `armyCap`) via `Titles::Unlock`. Cosave `TITL` v1. Debug F4.
- Títulos da facção do jogador por marcos (territórios, províncias, população, exército): ex. Chefe de bando →
  Senhor → Jarl → Rei/Rainha Supremo(a) de Skyrim. Tabela data-driven (`Titles.json`), textos en/pt-br.
- Cada título destrava coisas (mais cargos, doutrinas, teto de exército) e é anunciado na tela.
- Cosave junto do `FACT` (subir versão com migração) ou `TITL` v1.

### M23. 🔄 Patches de compatibilidade + release — infra pronta ([ADR 0040](adr/0040-pipeline-de-release.md), [0041](adr/0041-fomod-layout.md), [0042](adr/0042-patches-de-compatibilidade.md))
- ✅ `.github/workflows/release.yml`: em tag `v*`, reaproveita o artefato do Build do mesmo commit
  (`tools/wait_build_run.py` espera ou dispara o build), monta o zip FOMOD e publica o GitHub Release com a
  seção do `CHANGELOG.md`. Passo a passo em [RELEASE.md](RELEASE.md).
- ✅ FOMOD (`fomod/`): núcleo obrigatório + patches opcionais (`patches/<nome>/`); `tools/package_release.py`
  monta e valida; testes em `tests/` (também no workflow `Tools` a cada push).
- ✅ Formato dos patches (`Compat/*.json`, `patches/README.md`) + patch `civil-war-mods` (nomes de plugin a verificar).
- ✅ `CHANGELOG.md`, rascunho da página do Nexus (`docs/nexus/`, en + pt-br).
- ✅ Relatório de performance do log: `tools/perf_report.py` + [PERFORMANCE.md](PERFORMANCE.md).
- ⬜ Leitura de `Compat/*.json` no DLL (TODO no ADR 0042, depois do bloco `conquest` da M5).
- ⬜ Pacotes JSON para mods populares de locais/fortes (JK's, Lux, Open Cities...) com coordenadas de baú/mesa
  da sede (depois da M6, medidas no jogo).
- ⬜ Perfil de performance num save grande (todos os territórios, guarnições, exércitos, IA, rotas, batalhas).
- ⬜ Primeiro release (tag `v0.1.0`) e upload no Nexus.

---

## Verificação
- **Toda missão:** CI verde no GitHub Actions (a partir da M1), artefato baixado e conferido.
- **Missões 🎮:** roteiro de teste curto que eu escrevo em cada missão (passos no jogo + o que procurar em
  `Documents\My Games\Skyrim Special Edition\SKSE\AgeOfSkyrim.log`); você executa e me manda o log.
- **Performance:** timings novos no log dentro da meta (< 1 ms por evento; ticks estratégicos < 5 ms).
- **Compatibilidade:** `AgeOfSkyrim.esp` sem nenhum override de registro vanilla (conferir no xEdit); carregar com um
  JSON que aponta para plugin ausente → aviso no log, sem crash.
- **Cosave:** em toda mudança de formato, testar save antigo → versão nova (migração) e save novo → recarregar.

## Execução
Uma missão por vez: implementação → CI verde → docs/ADRs → commit + push → (🎮) roteiro de teste do usuário → próxima.

**Atualização ([ADR 0021](adr/0021-implementar-tudo-teste-unico-no-fim.md)):** as missões restantes são implementadas sem esperar os testes no jogo; um roteiro único e consolidado em `TESTES.md` no fim cobre tudo.
