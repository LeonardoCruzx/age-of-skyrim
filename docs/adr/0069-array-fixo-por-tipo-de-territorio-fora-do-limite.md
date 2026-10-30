# 0069. Crash ao processar economia/população com um território "claim" ou "house" dono

- **Status:** Aceita
- **Data:** 2026-10-30
- **Origem:** Bug de usuário (jogo fechou sozinho ao apertar F6, depois de fundar a sede em Lakeview Manor)

## Contexto
Usuário reportou o jogo fechando sem nenhuma mensagem de erro no log (nada entre o F6 e o fim do arquivo) ao
apertar a hotkey de economia (F6) tendo um território tipo `"house"` (Lakeview Manor, M29) e um `"claim"`
(Free Claim, M8) já pertencendo à sua facção.

Causa: três lugares no código guardam um valor por `TerritoryType` num `std::array` de tamanho **fixo**,
indexado direto por `static_cast<std::size_t>(tipo)`, sem checar limite:
- `Economy::production_` (`Economy.h`): `kTypeCount = TerritoryType::kCity + 1` (6 posições: `kUnknown`..`kCity`).
  `Economy::Process()` (chamada pelo F6) faz `production_[static_cast<std::size_t>(t.type)]` pra **todo**
  território do dono, sem filtrar por tipo antes.
- `Garrison::initial_`/`max_` (`Garrison.h`): `kTypeCount = TerritoryType::kClaim + 1` (7 posições).
  `Garrison::LoadFile` escreve direto em `a_out[static_cast<std::size_t>(*type)]` se `Garrison.json` tiver uma
  chave de tipo (hoje não tem "claim"/"house", mas escreveria fora do limite se tivesse).
- `Realm::PopulationDef::initial` (`Data.h`): array fixo de 6 posições (literal, sem constante nomeada).
  `Realm::Data::InitialPopulationOf()` (chamada no mesmo caminho do F6, cálculo de população/hora) faz
  `population_.initial[static_cast<std::size_t>(a_territory.type)]` pra qualquer território dono.

`TerritoryType::kClaim` (M8) já existia e vale `6`; nenhum dos três arrays tinha 7 posições — ou seja,
**esse bug já existia antes desta sessão**, só nunca travou visivelmente: ler `production_[6]`/
`population_.initial[6]` 1 posição depois do fim do array lia lixo de memória adjacente (outro campo da
própria classe) que, por sorte, não travava nada (um `float`/`vector` "vazio" por acaso). A título 'Rebeldes'
mostrando "pop. 6" num log anterior desta sessão (antes da M29) já era esse exato lixo de memória sendo lido
e exibido como se fosse um número válido — não era um valor real.

`TerritoryType::kHouse` (M29, nesta sessão) é `7` — **2** posições depois do fim de `production_`/
`population_.initial`, e **1** depois do fim de `Garrison`'s arrays. Ler/escrever tão mais longe do array
real passou a estourar em memória que o programa de fato usa pra outra coisa, corrompendo estado e
travando o processo — sem excessão C++ nenhuma pra aparecer no log (é comportamento indefinido de C++, não
um erro que o `try/catch` pegaria).

## Decisão
Os três arrays passam a cobrir **todo** o enum atual (`TerritoryType::kHouse + 1` = 8 posições), não só os
tipos que os arquivos JSON padrão já usam:
- `Economy::kTypeCount` e `Garrison::kTypeCount`: trocado de `kCity + 1` / `kClaim + 1` para `kHouse + 1`.
- `Realm::PopulationDef::initial`: de `std::array<float, 6>` pra `std::array<float, 8>`, com `5.0f` (claim)
  e `20.0f` (house) nas duas posições novas — valores razoáveis (claim é só um estandarte, sem gente; house é
  uma casa com uma família, bem menor que uma vila), ajustáveis em `Population/*.json → initial` (`"claim"`/
  `"house"` já aceitos pelo parser, só não estavam documentados).

Alternativa descartada: trocar os três `std::array` fixos por `std::unordered_map<TerritoryType, ...>`
(sempre seguro, não importa quantos tipos existam). Descartada por agora: é uma mudança maior (reescreve
inicialização/leitura dos três) por um ganho que o `kTypeCount` correto já cobre; fica anotado aqui caso um
quarto tipo apareça de novo e alguém esqueça de atualizar os três lugares.

## Consequências
- Nenhum impacto de desempenho (array cresce de 6/7 pra 8 posições; ainda O(1) e cabe em cache).
- Território tipo "claim" e "house" agora têm população/produção corretas (antes liam lixo); "claim" nunca
  teve regra de produção própria em `Economy.json`, então continua sem produzir recurso (só população/mão de
  obra mudam) — nada de ganho ou perda de ouro muda pra claims.
- Risco residual anotado: se um futuro `TerritoryType` novo for adicionado sem atualizar os três `kTypeCount`/
  o array de `Data.h`, o mesmo bug volta. Comentário deixado nos três arquivos apontando um pro outro.
