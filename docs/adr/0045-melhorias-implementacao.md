# 0045. Melhorias de territórios: implementação (custo por categoria, obra no tick, visual ancorado)

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica

## Contexto
A [ADR 0020](0020-melhorias-de-territorios.md) decidiu melhorias com custo em itens reais, tempo de obra e visual
por nível. Faltava definir dados, pagamento, status, persistência e de onde vêm as coordenadas dos objetos.

## Decisão
- **Dados:** `Upgrades.json` + `Upgrades/*.json` (mesmo `id` substitui; `visuals` somam). Melhoria = `id`, `name`
  `{en, pt-br}`, `types` (vazio = todos) e `levels` (até 10; padrão I–III). Nível = `cost` por **categoria do
  estoque** (`Resources.json`), `hours` de obra e `stats` (`defense`, `capacity`, `income`, `vision`,
  `garrisonMax`). Status são o total do nível atual (não cumulativos entre níveis) e somam entre melhorias.
  Padrão: muralhas, quartel, torre de vigia, armazém, mercado, forja.
- **Pagamento:** facção do jogador paga do **baú da sede** (`Seat::Chest`): conta por categoria
  (`Stock::CountByCategory`), confere tudo e só então remove (`Stock::Take`, qualquer item da categoria). Facção de
  IA não paga aqui (a M16 decide com `Economy::Balance`).
- **Obra:** uma por território; `remaining` em horas, descontado em `Upgrades::OnHours`, pendurado no tick da
  economia ([ADR 0046](0046-ganchos-do-tick-da-economia.md)) — o F6 (horas forçadas) também avança obras. Troca de
  dono durante a obra = obra perdida (sem reembolso). Níveis prontos ficam com o território mesmo trocando de dono.
  Em obra, a força da guarnição × `constructionDefenseMultiplier` (0,75).
- **Status aplicados:** `garrisonMax` → `Garrison::Max`; `defense` → `Garrison::Strength`; `income` →
  multiplicador de produção por território (`Economy::SetTerritoryMultiplier`, 1 + renda%/100); `capacity` e
  `vision` só expostos (estoque sem limite ainda; mapa M11). Cache por território, consulta O(1).
- **Visual por nível:** `visuals[]` = `{territory, upgrade, level, anchor, objects[{base, offset, angle}]}`.
  `territory` aceita o id do JSON ou `"Plugin.esp|0xID"` do `BGSLocation` descoberto. Âncora = `worldLocMarker`
  (`marker`), uma bandeira do território (`banner`) ou o primeiro que existir (`auto`); offsets no referencial da
  âncora. Objetos criados por `Placement::PlaceAt(..., origem = âncora)` (persistentes, sem física) quando o nível fica
  pronto e a âncora está numa célula carregada; senão o território fica pendente e é tentado de novo em cada
  `TESCellFullyLoadedEvent` (sink sai na primeira linha sem pendências). Sem entrada = só status. Exemplo: muralhas
  I/II na `test_tower` (âncora = bandeira criada pelo F10; objetos vanilla de placeholder: lenha e lingote de ferro).
- **Cosave `UPGR` v1:** `u32 n × {Id território, u32 k × {Id melhoria, u8 nível}, u8 temObra, [Id melhoria, u8
  nível, f64 restante, f64 total, Id dono]}`, depois `u32 m × {Id chave do objeto, FormID}`. Melhoria removida do
  JSON é descartada no load.
- **API:** `Upgrades::Stat(t, Stat::kDefense/kCapacity/kIncome/kVision/kGarrisonMax)`, `Level(t, u)`,
  `InConstruction(t)`, `CurrentConstruction(t)`, `NextAvailable(t)`, `Start(t, u, pagar)`.
- Debug **NumPad6** (`debug.upgradeHotkey`, DIK 77): inicia a próxima melhoria do território atual do jogador (ou da
  sede); `debug.upgradeIgnoreCost` = obra grátis para teste.

## Consequências
- Balanceamento todo em JSON; objetos curados por local podem vir de patches (M28) sem recompilar.
- Objetos visuais não somem se o nível fosse perdido (hoje níveis nunca caem).
