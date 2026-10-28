# 0047. IA estratégica: avaliação a cada N horas, ataques como entidades numéricas (`WARS`)

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M16)

## Contexto
A M16 pede facções de IA que avaliam vizinhos e atacam, com resolução fora da tela, sem nada por frame, sem
Papyrus e sem atores (lições `ISSUE-CTD-01/04` do comparativo, ADR 0031). Guarnição (M14) e melhorias (M15)
estão sendo feitas em paralelo e ainda não existem; batalhas em tempo real são a M21.

## Decisão
- **Relógio:** a IA pendura no tick da economia (ADR 0026) por um gancho novo `Economy::AddTickListener`
  (chamado na thread principal, **fora** do lock da economia, depois de cada tick que processou horas).
  `Ai::OnHours` avança as marchas toda hora processada e roda uma avaliação a cada `ai.tickHours` (padrão 6).
  Catch-up longo (dormir 168 h) faz **uma** avaliação só.
- **Quem:** facções pré-definidas (não-jogador). **Alvos:** os `neighbors` dos territórios da facção (para claims,
  o inverso: vizinhos do claim). Alvo válido = dono em **guerra** com a facção (M17, ADR 0049), ou neutro dos
  `ai.expandTypes` com `ai.expandIntoNeutral`; nunca excluídos (ADR 0050); `ai.attackPlayer` desliga ataques ao
  jogador. Lote de `ai.maxEvaluationsPerTick` territórios a partir de um cursor rotativo.
- **Decisão por saldo e força:** força = `attackBase + ouro / goldPerStrength`; o ouro gasto (do saldo numérico
  `Economy::Balance`, via `Economy::Spend`) é o necessário para `defesa × minAdvantage × 1,25`, limitado a
  `[minGoldPerAttack, maxGoldPerAttack]`. Sem ouro, sem ataque. Escolhe o alvo mais barato (empate: inimigo em
  guerra antes de neutro). No máximo um ataque novo por facção por avaliação, `maxAttacksPerFaction` em
  andamento, `maxActiveAttacks` no total e `cooldownHours` entre lançamentos.
- **Defesa:** interface `Ai::SetDefenseProvider(fn(territory, owner))`. Padrão = `ai.defenseByType` (neutro ×
  `neutralDefenseMultiplier`). M14/M15 registram `base + Garrison::Strength + Upgrades::Stat(Defense)`.
- **Ataque = entidade** (id `attack/<serial>`, atacante, origem, alvo, defensor, força, horas restantes/total) no
  cosave **`WARS` v1** (+ horas desde a última avaliação, serial e esperas por facção). Marcha =
  `distância/10000 × hoursPer10000Units`, entre `minTravelHours` e `maxTravelHours`.
- **Resolução:** na chegada, revalida (o dono mudou? acabou a guerra? → cancela). Força × (1 ± `randomness`,
  sorteio determinístico por id do ataque e hora) contra a defesa atual; vitória → `SetTerritoryOwner`.
- **Perto do jogador:** alvo do jogador e jogador no local (ou num sub-local) ou a menos de `nearPlayerRadius`:
  `Ai::OnAttackArrivedNearPlayer` notifica "X está sob ataque"; um `NearPlayerHandler` (M21) pode assumir
  (`engaged`) e terminar com `Ai::ResolveAttack(id, venceu)`. Sem handler, resolução numérica na hora. Ataque
  `engaged` não sobrevive ao save (volta a marchar 1 h).
- **Agressão:** `"aggression"` (0..1) por facção em `Factions/*.json`: vizinho em paz com poder ≤ o da facção /
  `declareWarRatio` pode receber declaração de guerra (uma por facção por avaliação).
- **Donos iniciais:** `"start"` por facção em `Factions/*.json` (locais + mesma província, por tipo) preenche o
  `defaultOwner` de territórios sem dono no JSON (`Config::SetDefaultOwner`, antes de `State::ResetToDefaults`).
  As vanilla começam com Solidão/Markarth (Legião) e Windhelm/Riften (Mantos) e as províncias delas; Whiterun
  começa neutra. FormIDs das cidades escritos de memória — aviso no log se algum não resolver.
- **Consultas para o mapa (M11):** `Ai::ActiveAttacks()`, `Ai::Threats(territory)`, `HoursToNextTick()`.
- **Debug:** NumPad7 (`debug.aiHotkey`) roda uma avaliação agora e lista até 5 ataques na tela (todos no log,
  com saldo e poder de cada facção).

## Consequências
- Custo: O(territórios × vizinhos) por avaliação, 1× a cada 6 h de jogo; tempo no log (`[tempo] Ai::Strategic`,
  `Ai::OnHours`). Meta < 5 ms.
- Sem `aggression` nem `start`, a IA de outro mod só reage a guerras declaradas (nada quebra).
- Com `expandIntoNeutral` a Legião e os Mantos vão pintando o mapa aos poucos (≈ 1 ataque/dia por facção com os
  padrões); desligue para um mundo parado.
- Bounty da facção antiga ao trocar dono de cidade (`ISSUE-GAME-02`) fica para quando houver guardas (M14/M21).
