# 0007. Objetos do mod criados em runtime, sem editar células no ESP

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Técnica

## Contexto
Pré-colocar bandeiras, baús e mesas em células vanilla pelo ESP sobrescreve o registro da célula e conflita com mods de cidades/iluminação; também exigiria trabalho manual no Creation Kit.

## Decisão
O DLL cria os objetos (`PlaceObjectAtMe`, depois por coordenadas do JSON) e guarda o FormID de cada referência criada no cosave (registro `PLAC`). O ESP, quando existir, só define objetos base novos.

## Consequências
- Compatível por construção; sem trabalho no CK.
- Coordenadas por local precisam de patch JSON quando outro mod muda o layout do forte.
