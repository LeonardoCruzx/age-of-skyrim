# 0016. Territórios descobertos automaticamente dos locais do jogo

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Técnica (proposta na conversa e aceita ao paralelizar a Fase 1)

## Contexto
O plano original da M4 era escrever à mão, em JSON, centenas de territórios vanilla com o FormID de cada
`BGSLocation`. Sem acesso ao `Skyrim.esm` no ambiente de desenvolvimento, esses IDs viriam da memória
(propensos a erro), e fortes/vilas adicionados por outros mods ficariam de fora.

## Decisão
Em `kDataLoaded`, o DLL varre todos os `BGSLocation` carregados (`World/Discovery`) e cria um território para
cada local com keyword vanilla de tipo:

| Keyword | Tipo |
|---|---|
| `LocTypeCity` | city |
| `LocTypeTown`, `LocTypeSettlement`, `LocTypeOrcStronghold` | village |
| `LocTypeMilitaryFort` | fort |
| `LocTypeMilitaryCamp`, `LocTypeBanditCamp`, `LocTypeForswornCamp`, `LocTypeGiantCamp` | camp |

- Keywords resolvidas por EditorID numa passada única (BGSKeyword guarda o EditorID mesmo sem po3 Tweaks).
- **ID estável:** `MakeId("Plugin.esp|0xLOCAL")` do local — independe da ordem de carga (ESL incluso).
- **Província:** primeiro ancestral (`parentLoc`) com `LocTypeHold`. **Posição:** marcador do mapa (`worldLocMarker`).
- **Vizinhos:** k mais próximos (padrão 4) no mesmo worldspace, calculados uma vez no load.
- Sub-locais de outro candidato são descartados (o local de fora representa o território).
- **JSON continua mandando:** um território com `"location": "Plugin.esp|0xID"` se liga ao local e só herda do jogo
  o que não declarou (nome, tipo, província, posição). `"tower"` existe só via JSON (o jogo não tem keyword de torre).
- `settings.json` → `discovery` (`enabled`, `types`, `exclude`, `neighbors`). Lookup O(1) local → território
  (`Config::FindByLocation`) para a conquista da M5.
- Com debug ligado, a lista completa vai para `SKSE/AgeOfSkyrim_territories.json`; o log traz um resumo PASS/FAIL.

## Consequências
- Compatível por construção: fortes, vilas e acampamentos de outros mods entram sem patch.
- Nomes vêm do jogo (`GetFullName`), já no idioma instalado; não passam pelas traduções do mod.
- Um custo único no carregamento (medido no log com `[tempo]`); nada roda durante o jogo.
- Territórios descobertos começam neutros e sem bandeiras; o cosave `TERR` passa a ter centenas de entradas
  (8 bytes cada).
- Tipos dependem das keywords que os autores de mods aplicam; casos errados se corrigem por JSON ou `exclude`.
