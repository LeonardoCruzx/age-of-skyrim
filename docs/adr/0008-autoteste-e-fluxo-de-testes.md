# 0008. Eu faço tudo; o usuário só testa, com roteiro curto e autoteste

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Decisão do usuário

## Contexto
O usuário não quer fazer trabalho de Creation Kit, buscar FormIDs nem preparar ambiente.

## Decisão
Toda a implementação, dados e configuração ficam comigo. Cada missão que precisa do jogo traz um roteiro de ≤ 5 passos em `TESTES.md` e um autoteste no DLL que escreve PASS/FAIL na tela e no `AgeOfSkyrim.log`. O usuário instala o zip do CI, segue o roteiro e envia o log.

## Consequências
- Preferir soluções que dispensam o CK (ADR 0007).
- Diagnóstico depende de logs bons.
