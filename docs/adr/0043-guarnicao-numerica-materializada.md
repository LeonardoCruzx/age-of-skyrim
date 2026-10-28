# 0043. Guarnição numérica, materializada só perto do jogador

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica

## Contexto
A M14 dá guardas aos territórios. O Conquest of Skyrim trava com dezenas de atores e Papyrus congestionado
(`ISSUE-CTD-01/04`, [comparativo](../COMPARATIVO-CONQUEST-OF-SKYRIM.md) seção 2). Centenas de territórios com
atores permanentes custariam memória, IA e tamanho de save, e editariam células vanilla.

## Decisão
- **Número, não ator:** a guarnição é `soldados (double)` por território + facção dona, no cosave **`GARR` v1**
  (`u32 n × {Id território, Id dono, f64 soldados}`, depois `u32 m × {FormID ator vivo}`). Território sem registro
  usa `initial[tipo]` de `Garrison.json` (limitado ao teto) — facções de IA têm guarnição sem custo.
  Teto = `max[tipo]` + status `garrisonMax` das melhorias (M15, [ADR 0045](0045-melhorias-implementacao.md)).
- **Troca de dono:** o listener de conquista (`Conquest::AddListener`) e uma reconciliação preguiçosa (registro com
  dono ≠ dono atual = guarnição inicial do novo dono) cobrem qualquer caminho de `SetTerritoryOwner` (IA, API).
  Atores do dono antigo somem.
- **Materialização:** refresh coalescido (uma `AddTask`) em `TESCellFullyLoadedEvent` e na troca de local do
  jogador. Ativo = território com dono cuja âncora — `worldLocMarker` do `BGSLocation`, senão uma bandeira do
  território — está numa célula **anexada** e a até `garrison.spawnRadius`. Filtro só matemático (worldspace +
  distância) antes de resolver a âncora. Interiores: o marcador é exterior, então a guarnição fica do lado de fora.
- **Lotes por frame + teto global:** os jobs entram numa fila; cada frame cria no máximo `garrison.spawnPerFrame`
  atores e reagenda (`AddTask` encadeado). Nunca mais de `garrison.maxActors` vivos no total. Posição: círculo em
  volta da âncora (`spreadRadius`), sem navmesh nem célula nova — o ator cai no navmesh vanilla do local.
- **Despawn:** `TESCellAttachDetachEvent` com `attached = false` de um ator nosso → `Disable()` + `SetDelete(true)`.
  O sink só olha refs dinâmicas (`0xFF…`) e sai na primeira linha sem atores vivos. Atores gravados num save são
  apagados no pós-load e recriados pelos números.
- **Mortes:** `TESDeathEvent` de um ator nosso → `soldados − 1`. O cadáver fica até a célula descarregar.
- **Manutenção e deserção:** `Economy::RegisterUpkeep("garrison")` cobra `upkeep` por soldado/hora (padrão:
  pão 0,02 e ouro 0,1) das facções do jogador (`aiUpkeep` liga para a IA). No tick (`AddTickListener`, [ADR
  0046](0046-ganchos-do-tick-da-economia.md)): déficit há mais de `deficitGraceHours` → `desertPerHour`; senão
  `reinforcePerHour` até o teto.
- **API interna:** `Garrison::Size/Max/Strength/Present(território)`, `Set/Add` (M16). Força = soldados ×
  `strengthPerSoldier` × (1 + defesa%/100) × `constructionDefenseMultiplier` durante obra.
- Debug **NumPad5** (`debug.garrisonHotkey`, DIK 76): mostra a guarnição do território atual (ou da sede /
  `debug.territory`) e força o spawn em volta do jogador (atores "forçados" só somem ao descarregar a célula).

## Consequências
- Custo fixo: nenhum ator longe do jogador; refresh = O(territórios) só em carga de célula.
- Guarnição dentro de interiores e pontos de patrulha curados ficam para depois (M21 usa o mesmo mecanismo).
- O jogador pode "farmar" a reposição matando a própria guarnição — aceito no MVP.
