# Age of Skyrim — Roadmap

Status: ✅ concluída · 🔄 em andamento · ⬜ a fazer · 🎮 precisa de teste no jogo (roteiro em [TESTES.md](../TESTES.md)).
As decisões por trás deste plano estão em [docs/adr](adr/README.md).

## Decisões já tomadas
- Conquista = **local limpo** ([ADR 0009](adr/0009-conquista-por-local-limpo.md)).
- UI = **MCM primeiro, PrismaUI depois**, sobre a mesma API nativa ([ADR 0010](adr/0010-ui-mcm-depois-prismaui.md)).
- **Sede** entre candidatas, **estoque** com itens reais por categoria, **mesa de guerra** com mapa tático
  ([ADR 0011](adr/0011-sede-entre-candidatas.md), [0012](adr/0012-estoque-itens-reais.md), [0013](adr/0013-mesa-de-guerra-mapa-tatico.md)).
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

### M3. ⬜ Facções do jogador (modelo + cosave)
- `src/Core/Factions.{h,cpp}`: `Faction { Id id; std::string name; Id style; std::uint32_t color; bool isPlayer; }`.
- Registro de cosave `FACT` v1. O dono de território passa a ser **facção** (não estilo): `TERR` v2 com migração
  (v1: o estilo gravado vira uma facção pré-definida de mesmo nome).
- Facções pré-definidas no JSON (`factions` em `Territories/*.json` ou novo `Factions.json`): Império, Stormcloaks.
- `BannerController` resolve facção → estilo para escolher as bandeiras.
- Hotkey de debug passa a ciclar entre facções.

### M4. ⬜ Territórios vanilla + tipos de local
- Arquivos JSON por região (`Territories/whiterun.json`, ...) com fortes, torres, acampamentos, vilas e cidades.
- Campos novos por território: `location: "Skyrim.esm|0xID"` (para M5), `type` (fort/tower/camp/village/city),
  `province` (hold), `position` (x,y do mapa) e `neighbors` — dados que o mapa tático (M11) e a IA (M15) usam.
- Bandeiras pré-colocadas no ESP só onde necessário; lista de trabalho no CK (você coloca, eu gero o JSON).

### M5. ⬜ 🎮 Conquista por "local limpo"
- Sink `TESLocationClearedEvent` → lookup O(1) `BGSLocation` → território → `SetTerritoryOwner(território, facção do jogador)`.
- Notificação na tela + log. Opção em `settings.json` para exigir que o jogador esteja no local.
- Re-spawn de inimigos não reverte o dono; só a guerra (M15) reverte.

### M6. ⬜ 🎮 Sede da facção (capital)
- No JSON, territórios candidatos marcados com `"seat": { ... }` (1–2 por província, fortes/torres); outros mods podem
  adicionar candidatos com o mesmo formato.
- Ao conquistar um candidato sem ter sede: prompt "Fundar sede aqui?" → registro de cosave `SEAT` v1.
- **Baú de estoque e mesa de guerra colocados em runtime** (`PlaceObjectAtMe` em `seat.chest` / `seat.warTable`:
  posição + rotação no JSON), FormIDs criados salvos no cosave. Zero edição de célula vanilla no ESP.
- Base objects (baú, mesa) definidos no `AgeOfSkyrim.esp` (ESL), sem tocar nos vanilla.
- Mudar de sede: remove os objetos antigos (transferindo o conteúdo do baú) e cria na nova.

### M7. ⬜ 🎮 Estoque por categoria
- O baú guarda **itens reais**; o DLL classifica por keyword padrão do jogo (`VendorItemFood`, `VendorItemOreIngot`,
  `VendorItemIngredient`, ouro `Gold001`, ...) → categorias Comida, Ouro, Metais, Materiais, Equipamento.
- Mapeamento keyword → categoria em JSON (`Resources.json`), extensível: itens de outros mods entram automaticamente
  se usam as keywords padrão; mods com keywords próprias adicionam linhas.
- Recontagem só no evento de fechar o baú (`MenuOpenCloseEvent` do ContainerMenu com o ref da sede) + cache;
  zero custo contínuo. Totais expostos na API (M9) e no mapa (M11).

### M8. ⬜ 🎮 Free Claim (estandarte em local genérico)
- Item/power "Fincar Estandarte": raycast (`bhkPickData`) do jogador até o chão, normal do terreno → `PlaceObjectAtMe`
  + rotação alinhada.
- Território dinâmico salvo no cosave (`CLAM` v1: posição, worldspace/célula, facção, FormID do ref criado).
- Limites: distância mínima entre claims, máximo por facção (config).

## Fase 2 — Interface e gestão

### M9. ⬜ Camada de API (Papyrus + SKSE para outros mods)
- `src/Api/ModApi.{h,cpp}` + `include/AgeOfSkyrimAPI.h` público: interface versionada via
  `SKSE::GetMessagingInterface()` (outros DLLs pedem a interface em `kPostLoad`), eventos `OnTerritoryChanged`,
  `OnFactionCreated`, `OnSeatFounded`, `OnStockChanged`.
- Evento Papyrus equivalente (`ModEvent "AoS_TerritoryChanged"` etc.) para mods só-script.
- `src/Papyrus/Api.{h,cpp}`: natives em `AgeOfSkyrim.psc` — facção, territórios/donos, sede, estoque, setar dono (debug).
- `data/Scripts/Source/AgeOfSkyrim.psc` + script compilado no CI (Caprica, fora do CK).
- Base do MCM (M10) **e** do mapa PrismaUI (M11).

### M10. ⬜ 🎮 MCM (SkyUI)
- `AgeOfSkyrimMCM.psc` (quest + `SKI_ConfigBase`): "Facção" (criar/renomear/estilo/cor), "Territórios" (lista + dono),
  "Sede & Estoque" (totais por categoria), "Debug".
- Quest do MCM adicionada ao ESP (passo a passo do CK).

### M11. ⬜ 🎮 Mesa de guerra + mapa tático (PrismaUI)
- Ativar a mesa de guerra da sede (evento de ativação filtrado pelo ref da sede) abre a view PrismaUI.
- Mapa de Skyrim com: territórios por tipo e cor da facção dona, províncias, vizinhança, estoque da sede,
  painel de cada local (tipo, dono, guarnição, renda, rotas que passam por ele).
- Ações no mapa: marcar alvos de ataque, traçar rotas de comércio (M13), ver ameaças (M15).
- PrismaUI é dependência opcional, detectada em runtime: sem ela, a mesa abre um menu de listas vanilla
  (MessageBox / ListMenu) com as mesmas ações essenciais.
- Dados enviados para a view só ao abrir e quando mudam (sem polling JS ↔ C++).

### M12. ⬜ Economia
- Tick por hora de jogo (lote, < 5 ms): territórios geram recursos por tipo (vila → comida, mina → metais, cidade → ouro),
  depositados como itens no baú da sede (respeitando a classificação da M7).
- Consumo: guarnições comem comida e recebem ouro; falta de estoque → moral/desserção (gancho para M14).
- Registro de cosave `ECON` v1 (último tick, saldos pendentes).

### M13. ⬜ Rotas de comércio
- Rotas entre territórios da facção (ou com facções em paz, M16) traçadas no mapa; cosave `ROUT` v1.
- Rendem ouro por tick conforme distância/tipo dos pontos; rota passando por território inimigo pode ser cortada.
- Simulação fora da tela (sem spawn de caravanas no MVP; caravanas visíveis ficam como extensão futura).

### M14. ⬜ 🎮 Guarnição
- Ao conquistar, spawn de N guardas da facção (leveled list por estilo, definida no ESP) no marcador do território.
- Guardas registrados no cosave (`GARR` v1); limpeza ao perder o território. Teto global de NPCs configurável.
- Manutenção paga com o estoque da sede (M12).

## Fase 3 — Mundo vivo

### M15. ⬜ Facções de IA e contra-ataques
- Tick estratégico (a cada X horas de jogo): cada facção de IA avalia vizinhos (`neighbors`) e decide atacar,
  considerando guarnição e estoque.
- Ataque fora da tela: resolução por força. Na tela: spawn de grupo atacante + evento de defesa.
- Ameaças e ataques em andamento aparecem no mapa tático.

### M16. ⬜ Diplomacia básica
- Relações entre facções (guerra/paz/aliança) no cosave; afeta IA, rotas de comércio e hostilidade dos guardas.

### M17. ⬜ Patches de compatibilidade + release
- Pacotes JSON opcionais para mods populares de locais/fortes (JK's, Lux, Open Cities...) incluindo coordenadas
  ajustadas de baú/mesa da sede; lista de exclusão para mods de guerra civil.
- Perfil de performance num save grande (todos os territórios, guarnições, IA, rotas).
- Release: FOMOD (patches selecionáveis), página do Nexus, changelog; CI publica release em tag `v*`.

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
