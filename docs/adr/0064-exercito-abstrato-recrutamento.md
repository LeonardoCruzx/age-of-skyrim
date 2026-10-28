# 0064. Exército abstrato, recrutamento e manutenção (M20)

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica

## Contexto
A M20 (comparativo seção 2/3, [ADR 0031](0031-exercito-governo-populacao-titulos.md)) pede exército e
recrutamento. O Conquest of Skyrim trava com atores permanentes e Papyrus congestionado (`ISSUE-CTD-01/04`); a
lição (seção 2) é a mesma de `Garrison` (M14): número primeiro, ator só perto do jogador (isso é a M21).

## Decisão
- **Entidade abstrata, não ator:** `Army::Entity` = facção, território atual/destino, horas de marcha restantes,
  contagem por tipo de tropa (`units: vector<{troop, count (double)}>`, fracionário para a deserção acumular
  como a guarnição), oficiais (contagem simples) e escolta (bool). Cosave **`ARMY` v1** (exércitos + fila de
  treino em andamento). Nenhum ator é criado pela M20; a materialização só existe na M21 (`Battle`).
- **`Troops.json` (novo, `src/Sim/Troops.*`):** tabela de tropas de CAMPO separada de `Garrison.json` (guarnição),
  mas reaproveitando o MESMO formato e a MESMA estratégia de escolha automática de `ActorBase`
  (`autoArmorHint`/outfit, FaceGen vanilla pronto, [ADR 0044](0044-tropas-bases-vanilla-outfit-faccoes.md)) — por
  facção/estilo, com fallback `"default"`. Cada tropa também tem `strength`, `goldCost`,
  `equipCategory`/`equipAmount` (categoria do estoque, `Resources.json`) e `trainHours`.
  **Decisão de reaproveitamento:** a lógica de resolução (`ResolveForm`/`AutoSoldier`/`ArmorMatches`) fica
  DUPLICADA em `Troops.cpp` em vez de extraída de `Garrison.cpp` — evita arriscar uma regressão num módulo
  estável e fora do escopo desta missão; o formato de dados é idêntico, só os arquivos JSON são distintos.
  `Troops` é usada pela M20 (custo/treino) e pela M21 (ActorBase/outfit dos atores de batalha).
- **`Army::Recruit(facção, território, tropa, n)`:** verifica ouro (`Economy::Balance`) e equipamento
  (`Stock::CategoryIndex`/`CountByCategory` no baú da sede, só para facções do jogador — a IA não tem estoque
  real de itens) ANTES de consumir população, para nunca drenar `Realm::Population::Draft` e falhar depois.
  `Draft` pode entregar menos que `n` pedido; o custo final usa o que veio. Sucesso entra numa fila de treino
  (`Training`, horas de jogo) e só ao terminar soma ao exército do território (cria um se não existir).
  Multiplicador de custo: `Realm::Modifiers::GetFor(facção, território, RecruitCost)` (doutrina/cargos/título,
  M18/M22, já previsto no enum `Modifier` da M18/M19).
- **Marcha:** `Army::Move` só entre vizinhos (`Territory.neighbors`), horas pela distância — MESMA fórmula da
  marcha da IA (`hoursPer10000Units`, min/max), pendurada em `Economy::AddTickListener("army", ...)` (nunca por
  frame; tick por hora de jogo, ADR 0026/0046).
- **Oficiais/comandantes:** dado simples (`officers: uint32`, bônus `1 + officers × officerBonusEach` na força
  total). **Simplificação documentada:** sem NPC de oficial por ora (ao contrário dos cargos de M18, que já têm
  `AssignOfficer` com NPC); fica para quando a M20 ganhar UI própria.
- **Escolta:** `Army::SetEscort` só marca o exército e respeita um teto GLOBAL (`army.maxEscort`, soma por
  facção). **TODO documentado:** nenhum seguimento físico do jogador ainda (ficaria caro sem um sistema de
  "squad follow" próprio); a contagem/teto já existem para quando isso for implementado.
- **Manutenção e deserção:** `Economy::RegisterUpkeep("army")` soma `troop.upkeep` × contagem por facção (só do
  jogador, a menos que `army.aiUpkeep`); no tick, déficit por mais de `deficitGraceHours` horas →
  `desertPerHour` × horas, proporcional aos tipos presentes (mesmo modelo de `Garrison::OnHours`).
- **Confronto entre exércitos (gancho para a M21):** quando dois exércitos parados no mesmo território são de
  facções em guerra (`Diplomacy::AtWar`), `Army::CheckClashes` tenta um `ClashHandler` registrável
  (`Army::SetClashHandler`, assinatura igual ao padrão de `Ai::SetNearPlayerHandler` da M16); sem handler (ou
  handler recusando), resolução NUMÉRICA própria (`ResolveNumerically`): força × sorteio (± `battle.randomness`,
  mesmo `splitmix64` da IA), perdedor desfeito, vencedor sofre baixas proporcionais ao esforço.

## Consequências
- Nenhum ator nem lookup de ator custa nada fora da M21; o custo é O(exércitos) por hora de jogo.
- `Army::ApplyLosses`/`Disband` são chamados tanto pela resolução numérica quanto pela M21 ao fim de uma batalha
  real — um único caminho para aplicar baixas.
- Sem UI de comando (mapa tático/MCM) ainda: movimento e recrutamento só por API/hotkey de debug nesta missão.
