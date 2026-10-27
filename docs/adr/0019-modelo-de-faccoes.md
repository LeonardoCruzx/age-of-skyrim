# 0019. Facções como donas de territórios (pré-definidas no JSON + do jogador no cosave)

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Técnica (M3)

## Contexto
Na Fase 1 o dono de um território era um *estilo* de bandeira. O jogador precisa criar a própria facção
(nome, estilo, cor), e outras facções (Legião, Mantos da Tempestade, as de outros mods) precisam existir.

## Decisão
- O dono de um território passa a ser uma **facção**; a facção escolhe um estilo (ADR 0004) e uma cor (`#RRGGBB`).
- **Pré-definidas** em `Data/SKSE/Plugins/AgeOfSkyrim/Factions/*.json` (nome localizado en/pt-br). Outros mods
  adicionam arquivos na pasta. Não vão para o cosave: mudar o JSON vale para saves existentes.
- **Do jogador** criadas em runtime (`player_<n>`, número nunca reaproveitado) e salvas no registro `FACT` v1
  (id, estilo, cor, chave, nome). Nome livre, digitado pelo jogador — não é traduzido.
- `TERR` sobe para **v2** (mesmo layout, dono = facção). Saves com `TERR` v1 migram: o estilo gravado vira a
  primeira facção pré-definida com aquele estilo (as vanilla usam a mesma chave do estilo, então o Id é igual).
- Ordem de escrita do cosave: `FACT` antes de `TERR`, para o dono ser validado ao carregar.
- `defaultOwner` dos territórios é validado contra as facções ao iniciar um jogo; desconhecido → neutro.

## Consequências
- UI (MCM/PrismaUI) e conquista (M5) trabalham com facções, não com estilos.
- Se um estilo some do JSON, facções do jogador que o usavam caem no primeiro estilo disponível (com aviso no log).
