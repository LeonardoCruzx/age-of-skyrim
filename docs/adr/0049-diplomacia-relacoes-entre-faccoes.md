# 0049. Diplomacia: guerra, paz, aliança e trégua entre pares de facções (`DIPL`)

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M17)

## Contexto
A M17 pede relações entre facções que afetam a IA (M16), as rotas (M13, `CanTrade`) e a hostilidade dos
guardas (M14/M21), com ações para MCM (M10) e mapa (M11).

## Decisão
- **Relações:** paz, guerra, aliança e **trégua** (com fim em hora de jogo). Mesma facção ou neutro = paz.
- **Padrões data-driven** em `Factions/*.json` (qualquer arquivo da pasta, outros mods incluídos):
  `"defaultRelation"` (entre IAs), `"playerRelation"` (jogador × IA) e `"relations": [{a, b, relation}]`, com
  `"player"` = qualquer facção do jogador. Vanilla: **Legião × Mantos = guerra**; o resto em paz.
- **Jogador × todos = paz** por padrão: a IA não ataca o jogador até haver guerra. A guerra começa quando o
  jogador declara, quando uma IA agressiva (ADR 0047) declara, ou **ao tomar um território de uma facção de IA**
  com quem não estava em guerra (`diplomacy.warOnConquest`, padrão ligado; vale também em trégua/aliança).
- **Cosave `DIPL` v1:** só as relações mudadas em jogo (`a, b, relação, fim da trégua`), por cima dos padrões —
  mudar o JSON vale para saves existentes nos pares nunca tocados.
- **Ações** (`Diplomacy::DeclareWar/ProposePeace/ProposeAlliance`, resultado enumerado com chave
  `diplomacy.result.*`; `SetRelation` para debug/API):
  - guerra: não durante trégua; rompe aliança;
  - paz (só em guerra): a IA aceita se `poder(IA) < poder(proponente) × peaceAcceptRatio` ou se está em déficit
    (M12); aceita → trégua de `truceHours` (72) e depois paz (aviso `diplomacy.truceEnded`, no tick);
  - aliança (só em paz): aceita se `poder(proponente) ≥ poder(IA) × allianceAcceptRatio` e, com
    `allianceNeedsCommonEnemy`, se há um inimigo em comum.
  - **Poder** = soma da defesa dos territórios (`Ai::DefenseOf`, o mesmo provedor da M14/M15) + ouro do saldo /
    `ai.goldPerStrength`. Para o jogador, o ouro é só o pendente (o baú real não entra; simplificação).
- **Efeitos:** IA só ataca em guerra; `Routes::CanTrade` = mesma facção, paz ou aliança (trégua não comercia);
  rota só é **cortada** por território de facção em guerra com o dono (antes: qualquer facção diferente);
  `Diplomacy::AreHostile(a, b)` = guerra (para guardas, M14/M21).
- **Consultas:** `RelationOf`, `AtWar`, `CanTrade`, `TruceHoursLeft`, `Explicit()` (pares que não estão em paz),
  `EnemiesOf`, `Power`. Thread-safe, O(1) por par.
- **Debug:** NumPad8 (`debug.diplomacyHotkey`) alterna paz → guerra → trégua → aliança da facção do jogador com o
  dono do território onde ele está.

## Consequências
- Alianças ainda não puxam aliados para a guerra (chamado às armas fica para a Fase 4 com exércitos, M20).
- A IA não propõe paz sozinha: guerras entre IAs duram até alguém mudar via API/debug.
