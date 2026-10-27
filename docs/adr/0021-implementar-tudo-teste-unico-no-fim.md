# 0021. Implementar as missões restantes sem esperar testes no jogo; teste único no fim

- Status: Aceita
- Data: 2026-10-27
- Tipo: Decisão do usuário

## Contexto
As missões M2–M8 estão implementadas e compilam no CI, mas ainda não foram testadas no jogo. O plano original
(ADR 0015) esperava o teste de cada missão 🎮 antes de seguir para a próxima.

## Decisão
O usuário pediu para seguir sem os testes: as missões M5, M6, M9–M23 são implementadas em sequência (com
paralelismo em worktrees quando os arquivos não se sobrepõem), cada uma validada só pelo CI (compilação +
checagem de traduções). No fim, um **roteiro de teste único e consolidado** em `TESTES.md` cobre tudo, com o
autoteste (F11) ampliado para verificar o máximo possível sozinho.

## Consequências
- Erros de comportamento no jogo (FormIDs, offsets, sinal de rotação, eventos) só aparecem no teste final;
  por isso cada módulo novo registra no log o que resolveu/fez e falha de forma segura (aviso, sem crash).
- Missões posteriores podem depender de suposições ainda não confirmadas (ex.: objetos em runtime da M2).
  Correções vindas do teste final entram como uma rodada de ajustes depois da última missão (M23, ADR 0031).
