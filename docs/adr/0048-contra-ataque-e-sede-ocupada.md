# 0048. Contra-ataque: território do jogador perdido e sede ocupada

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M16)

## Contexto
Com a IA (ADR 0047), territórios do jogador podem ser tomados. O território da sede (M6, ADR 0023) tem baú de
estoque com **itens reais** do jogador e a mesa de guerra; a economia (M12) entrega a produção nesse baú.

## Decisão
- Território do jogador tomado: `SetTerritoryOwner` para o atacante (bandeiras trocam), notificação
  `ai.lost`; rotas que dependiam dele ficam inativas sozinhas (avaliadas no tick, ADR 0028).
- **Sede tomada → "sede ocupada":** os objetos **ficam** (baú, itens e mesa não são apagados nem movidos — perder
  itens reais por uma simulação numérica seria punitivo e irreversível), mas o **depósito da economia é suspenso**
  (`Economy::SetDepot(nullptr)`): a produção volta a acumular como pendente (cosave `ECON`). Notificação
  `ai.seatLost`.
- Retomar o território (conquista M5) religa o depósito (`Economy::SetDepot(Seat::Chest())`), entrega o pendente
  e notifica `ai.seatRecovered`. Mudar a sede para outro território (`Seat::Found`) também resolve.
- O estado "ocupada" não vai para o cosave: é derivado (dono do território da sede ≠ facção do jogador) e
  recalculado em `Ai::OnPostLoad`, depois do `Seat::OnPostLoad`.
- Não há "fim de jogo" ao perder a sede.

## Consequências
- O jogador pode ir até o baú e pegar os itens mesmo com a sede ocupada (sem guardas inimigos até a M14/M21).
- Seat não precisou de API nova; tudo vive em `Ai` (`depotSuspended_`).
