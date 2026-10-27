# 0030. Dogfish / Conquest of Skyrim como referência de design (sem copiar conteúdo)

- Status: Aceita
- Data: 2026-10-27
- Tipo: Decisão do usuário

## Contexto
O usuário sugeriu usar o projeto [Dogfish](https://github.com/bearingme/Dogfish), uma pesquisa que recupera a
especificação do mod Conquest of Skyrim (o principal mod de grande estratégia para Skyrim SE), como fonte.
O Dogfish é só documentação (inventário de funcionalidades e de problemas conhecidos), sem licença declarada e
com vários itens marcados como não verificados. O Conquest of Skyrim é obra de outro autor.

## Decisão
- Dogfish e Conquest of Skyrim entram como **referências de design**, listadas em `docs/REFERENCIAS.md`.
- Uso permitido: funcionalidades como checklist de requisitos, problemas conhecidos como lições do que evitar,
  escritos com as nossas palavras e citando os IDs `FEAT-*`/`ISSUE-*` deles para rastreabilidade.
- Proibido: copiar textos, scripts, código, assets, FormIDs ou dados do Conquest of Skyrim ou do Dogfish, e
  inspecionar a implementação do mod original (o próprio Dogfish adia isso nos seus limites).
- O comparativo fica em `docs/COMPARATIVO-CONQUEST-OF-SKYRIM.md`. As lições da seção 2 viram requisitos das
  missões indicadas, e as funcionalidades da seção 3 só entram no roadmap com decisão do usuário.

## Consequências
- Ganhamos um checklist do que os jogadores esperam do gênero e dos pontos em que ele já falhou (desempenho com
  muitos atores, navmesh, outfits, conflito com a Guerra Civil), alinhado ao ADR 0005.
- Itens `UNV` do Dogfish são tratados como hipóteses e nunca como fatos sobre o motor do jogo.
