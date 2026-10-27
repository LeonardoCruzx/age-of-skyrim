# 0022. Conquista por local limpo: implementação

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Técnica (M5, detalha a [ADR 0009](0009-conquista-por-local-limpo.md))

## Contexto
A ADR 0009 decidiu que um território passa para a facção do jogador quando o `BGSLocation` dele é limpo. Na
CommonLibSSE-NG usada não existe `TESLocationClearedEvent`: o evento disponível é `RE::LocationCleared::Event`
(fonte `LocationCleared::GetEventSource()`), que **não traz o local**. Também era preciso decidir o que fazer se o
jogador ainda não tem facção, e como conviver com mods de guerra civil.

## Decisão
- **Gatilho**: sink de `LocationCleared::Event` só agenda uma task (`AddTask`). Na task, a cadeia de locais do
  jogador (`GetCurrentLocation()` → `parentLoc`, até 16 níveis, mais interno primeiro) é percorrida e o primeiro local
  que **está limpo** (`IsCleared()`) e tem território (`Config::FindByLocation`, O(1)) é conquistado. A leitura no
  frame seguinte garante que o flag já foi gravado. Nada encontrado → log com a cadeia inteira.
- **`requirePlayerInLocation`** (padrão `true`): só a cadeia atual. Com `false`, também vale o local de onde o jogador
  acabou de sair (guardado por um sink de `TESActorLocationChangeEvent` filtrado pelo jogador — uma comparação por
  evento), para o caso de o último inimigo morrer logo depois da saída. Locais distantes não podem ser detectados,
  porque o evento não diz qual local foi limpo; varrer todos os locais limpos daria territórios limpos por quests ou
  antes da instalação do mod de uma vez só.
- **Facção do jogador**: se ainda não existe, a primeira conquista (ou o primeiro Free Claim) cria uma padrão:
  nome traduzido (`conquest.defaultFactionName`), estilo `conquest.defaultStyle` ou, vazio, o primeiro estilo que
  nenhuma facção pré-definida usa (senão o último declarado), cor `conquest.defaultColor`. O jogador renomeia depois
  (MCM, M10).
- **Dono anterior**: por padrão a conquista toma de qualquer facção (limpar um forte imperial = derrotar a guarnição).
  `onlyFromNeutral: true` só toma de neutros. Já sendo do jogador: nada acontece.
- **Compatibilidade**: `conquest.exclude` (BGSLocation `"Plugin.esp|0xID"` ou id de território do JSON) e
  `conquest.excludePlugins` (nome do plugin do local) ignoram locais que outro mod controla.
- **Re-spawn**: nada reage ao local "sujar" de novo; só a guerra (M16) devolve. Re-limpar um território já do jogador
  não faz nada.
- **Ganchos**: `Conquest::AddListener(território, dono anterior, origem)` roda depois de cada conquista (a sede usa;
  M9 vai expor `OnTerritoryChanged`). `ConquerForPlayer(território, origem)` é a entrada para outros módulos.
- **Debug**: `debug.conquestHotkey` (F7, DIK 65) conquista o território do local atual sem limpar; fora de qualquer
  território usa `debug.territory`. Se o território já é do jogador, os ganchos rodam de novo (para repetir o teste
  da sede).
- Mudança de dono sempre por `SetTerritoryOwner`; `ScopedTimer` em `Conquest::ConquerForPlayer`.

## Consequências
- O comportamento depende de o jogo setar `cleared` antes da task rodar (esperado; conferir no teste: o log mostra
  a cadeia com "limpo").
- Limpar um sub-local sem território (ex.: uma caverna dentro de uma cidade) não conquista o local de fora, porque
  só conta um local que está limpo ele mesmo.
