# 0038. Títulos por marcos, avaliados só em eventos, que nunca rebaixam

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M22)

## Contexto
A M22 pede títulos (Chefe de Bando → Senhor → Jarl → Rei/Rainha Supremo(a)) por marcos, com desbloqueios e
anúncio na tela, sem custo contínuo.

## Decisão
- `Titles/*.json` (extensível, id repetido substitui) em ordem de `rank`. `requires`: `territories`, `provinces`
  (províncias com maioria, mesma regra dos governadores), `cities`, `population` — todos precisam ser atingidos.
- **Avaliação só em eventos:** troca de dono (`OnTerritoryOwnerChanged`) e tick (`OnHoursPassed`, pelos marcos de
  população), numa passada O(territórios) (`Realm::ComputeStats`). Só facções do jogador recebem título.
- **Títulos só sobem:** perder territórios não rebaixa — evita anúncios "sobe/desce" em guerra e desbloqueios que
  somem com cargos já nomeados. Rebaixamento, se um dia vier, será outra regra (ex.: estabilidade).
- O título inicial (rank mais baixo) é atribuído em silêncio; subir é anunciado (`realm.titleUp`, opção
  `realm.announceTitles`). Nome no gênero do jogador (`nameFemaleKey`: Senhora, Rainha Suprema).
- **Desbloqueios como dados:** `unlocks` numéricos (`officers`, `governors`, `armyCap`, ... livres para outros mods),
  acumulados por rank no load; consulta `Titles::Unlock(facção, chave, padrão)`. Doutrinas e cargos usam
  `requiresTitle`. Títulos também podem dar `modifiers`.
- Cosave `TITL` v1: `{u32 facção, u32 título}`; título que sumiu do JSON é reavaliado no próximo evento.

## Consequências
- M20 lê `Unlock(..., "armyCap", ...)` para o teto de exército; M10/M11 mostram título e próximo marco.
- Marcos são ajustáveis só no JSON; o debug F4 (conquistar próximos) serve para testar a subida.
