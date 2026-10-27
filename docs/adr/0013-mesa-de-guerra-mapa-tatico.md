# 0013. Mesa de guerra na sede abre mapa tático em PrismaUI

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Decisão do usuário

## Contexto
O jogador precisa planejar ataques, rotas de comércio e ver dados de acampamentos, torres, cidades e vilas.

## Decisão
Uma mesa de guerra (criada em runtime na sede) abre um mapa interativo em PrismaUI com territórios por tipo e dono, painel de cada local, alvos e rotas. Sem PrismaUI, a mesa abre menus vanilla com as ações essenciais. Dados enviados só ao abrir e quando mudam.

## Consequências
- PrismaUI vem antes do previsto no roadmap.
- Territórios precisam de `type`, `province`, `position` e `neighbors` no JSON.
