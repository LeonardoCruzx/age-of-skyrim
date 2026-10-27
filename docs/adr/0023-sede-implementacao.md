# 0023. Sede da facção: candidatas, pergunta, objetos e cosave

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Técnica (M6, detalha a [ADR 0011](0011-sede-entre-candidatas.md) e a [ADR 0007](0007-objetos-em-runtime.md))

## Contexto
A sede é escolhida entre fortes/torres candidatos, com baú de estoque e mesa de guerra criados em runtime. Faltava
decidir onde declarar as candidatas sem FormIDs confiáveis, como perguntar ao jogador sem ESP/Papyrus, onde pôr os
objetos sem coordenadas curadas, e como guardar tudo.

## Decisão
- **Candidatas em `Seats/*.json`** (pasta própria; outros mods adicionam arquivos):
  - `auto`: em cada província (Hold), os `perProvince` (padrão 2) territórios de `types` (padrão fort/tower) mais
    próximos da cidade da província (sem cidade: ordem da chave). Usa os territórios descobertos (ADR 0016), então
    funciona com mods de locais e **não depende de FormIDs de memória** — nenhum `BGSLocation` vanilla foi listado à
    mão porque eu não tenho IDs com confiança suficiente.
  - `candidates`: lista explícita por `territory` (id do JSON) ou `location` (`"Plugin.esp|0xID"`), com
    `chestOffset`/`warTableOffset` opcionais. `exclude` remove candidatas de qualquer arquivo já lido.
  - O vanilla inclui `test_tower` (território de teste) para testar a sede em qualquer lugar.
- **Pergunta**: ao conquistar uma candidata (gancho da conquista, ADR 0022), `MessageBoxData` vanilla com Sim/Não
  (`UI/Prompt`: fábrica `MessageDataFactoryManager` + `IMessageBoxCallback` próprio; índice do botão = valor − 4,
  padrão de outros plugins NG — o valor bruto vai para o log). Sem sede: "Fundar sede aqui?"; com sede em outro
  lugar: "Mudar a sede?". Se a fábrica não existir, funda direto (só a primeira sede; mudança sempre pede confirmação).
  `seat.askBeforeFounding: false` funda a primeira sem perguntar.
- **Posição dos objetos**: sem coordenadas curadas, baú e mesa nascem **relativos ao jogador no momento da fundação**
  (`chestOffset` / `warTableOffset`, [direita, frente, cima], virados para o jogador), via `Placement::PlaceAt`
  (persistentes). Coordenadas por local ficam para os pacotes da M18.
- **Objetos base**: ver [ADR 0025](0025-objetos-base-da-sede.md).
- **Cosave `SEAT` v1**: `u32 território, u32 FormID do baú, u32 FormID da mesa` (`ResolveFormID` na carga).
  Gravado depois dos outros registros (não depende de ordem).
- **Mudar de sede**: cria os objetos novos, move todo o inventário do baú antigo para o novo (`RemoveItem` com
  `kStoreInContainer`, uma chamada por `ExtraDataList` para manter encantamentos/melhorias), depois `Disable` +
  `SetDelete` nos antigos.
- **Estoque (M7)**: o baú é `Stock::Observe`-ado (recontado ao fechar o `ContainerMenu`) e recontado ao fundar e
  após carregar o save. `Seat::StockTotals()` devolve a última contagem por categoria. F8 mostra o inventário do
  jogador e, se houver sede, o estoque dela.
- **Mesa de guerra (M11)**: sink de `TESActivateEvent` filtrado pelo FormID da mesa (atômico, uma comparação por
  ativação) chama `Seat::OnWarTableActivated()`, que por enquanto só notifica e loga.
- **API** (M9/M10/M11/M12): `HasSeat`, `SeatTerritory`, `Chest`, `ChestId`, `WarTable`, `StockTotals`, `Candidates`,
  `IsCandidate`, `Found(território)`, `Offer(território)`. Getters thread-safe; ações na thread principal.

## Consequências
- Sede funciona sem nenhum ESP; os objetos podem nascer em lugar ruim (dentro de parede, ladeira) até existirem
  coordenadas curadas — o jogador escolhe onde está parado ao responder "Sim".
- Perder o território da sede (M16) ainda não remove a sede; fica para a guerra.
