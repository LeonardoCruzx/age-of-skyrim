# 0024. Free Claim vira território dinâmico (e ordem dos registros no cosave)

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Técnica (integração da M8, completa a [ADR 0018](0018-free-claim-raycast.md))

## Contexto
Um claim (estandarte fincado) precisa ser um território da facção do jogador que o resto do mod enxergue (mapa,
economia, guerra). Os territórios vêm do JSON + descoberta no `kDataLoaded` e ficam num `vector` cujos ponteiros
outros módulos guardam; claims existem por save e nascem durante o jogo. O `TERR` descarta, ao carregar, territórios
que não existem na config.

## Decisão
- **`Config::AddDynamic` / `ClearDynamic`**: territórios dinâmicos num `std::deque` separado (push_back não invalida
  ponteiros) com índice próprio sob `shared_mutex`. `Config::Find` procura nos estáticos e depois nos dinâmicos;
  `Territories()` continua só com os estáticos (sem custo novo para quem já o usa); `ForEachTerritory(fn)` percorre
  os dois. Província e vizinhos do claim vêm dos k territórios estáticos mais próximos (O(n) uma vez por claim).
- **Território do claim**: id = Id do claim (`claim/<n>`), tipo `claim` (novo `TerritoryType::kClaim`), nome
  traduzido `claim.territoryName`, posição/worldspace do estandarte, sem bandeiras de estilo.
- **Dono**: a facção do jogador (criada se ainda não existe, ADR 0022), por `SetTerritoryOwner`; fica no `TERR`
  como qualquer território.
- **Cosave `CLAM` v2**: + `u32 serial`, `u32 facção fundadora` por claim. v1 migra: serial recuperado pelo hash do Id,
  fundadora = facção do jogador.
- **Ordem de escrita**: `FACT` → `CLAM` → `TERR` → `PLAC` → `TEST` → `SEAT`. Na carga, o `CLAM` registra os
  territórios dinâmicos (com dono = fundadora) **antes** do `TERR`, que então restaura o dono salvo sem descartar os
  claims. Saves antigos (CLAM depois do TERR) não tinham donos de claims, então a fundadora vale.
- **Revert**: `Claims::Clear` (que limpa os dinâmicos) roda antes de `State::ResetToDefaults`.

## Consequências
- Consumidores que devem incluir claims (mapa M11, economia M12, guerra M16) usam `ForEachTerritory`/`Find`.
- Vizinhança é só do claim para os estáticos (os estáticos não ganham o claim como vizinho).
