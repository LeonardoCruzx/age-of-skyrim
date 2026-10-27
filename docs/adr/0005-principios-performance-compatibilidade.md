# 0005. Performance e integração com outros mods como prioridade

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Decisão do usuário

## Contexto
Load orders de Skyrim são enormes; mods que fazem polling, editam registros vanilla ou têm dependências rígidas quebram setups.

## Decisão
Toda missão segue: 100% orientado a eventos (sem polling por frame nem loops Papyrus); lookups O(1) pré-computados; mexer no mundo só quando o estado muda; cosave compacto; tempo das operações no log (meta < 1 ms por evento). **Zero override de registro vanilla** no ESP (ESL), ligação a locais vanilla via JSON `Plugin.esp|0xID`, tolerância a plugins ausentes, dependências opcionais detectadas em runtime e API pública cedo.

## Consequências
- Algumas features ficam mais trabalhosas (ex.: colocar objetos em runtime, ADR 0007).
- Facilita patches e convivência com Lux, JK's, Open Cities etc.
