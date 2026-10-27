# 0009. Conquista de território quando o local é limpo

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Decisão do usuário

## Contexto
Era preciso definir o gatilho de conquista de um território.

## Decisão
Um território passa para a facção do jogador quando o `BGSLocation` dele é marcado como limpo (todos os inimigos mortos), via evento do jogo (`TESLocationClearedEvent`) e lookup O(1) local → território. Re-spawn de inimigos não devolve o território; só a guerra (IA) devolve.

## Consequências
- Usa um mecanismo vanilla, compatível com mods que adicionam inimigos.
- Locais sem `BGSLocation` próprio usam Free Claim.
