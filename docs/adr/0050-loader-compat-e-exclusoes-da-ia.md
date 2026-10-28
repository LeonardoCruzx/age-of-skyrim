# 0050. Loader de `Compat/*.json` e exclusões da IA (locais excluídos e fortes da Guerra Civil)

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M16), completa o TODO do [ADR 0042](0042-patches-de-compatibilidade.md)

## Contexto
O ADR 0042 definiu o formato dos patches `Compat/*.json`, mas a leitura no DLL estava pendente. A M16 precisa
que a IA não ataque locais que outros mods (guerra civil) controlam.

## Decisão
- `Config::LoadCompat` roda em `kDataLoaded`, logo depois do `settings.json` e antes da descoberta. Lê
  `Data/SKSE/Plugins/AgeOfSkyrim/Compat/*.json` em ordem alfabética.
- `whenLoaded`: ativo se qualquer plugin estiver carregado (`TESDataHandler::LookupLoadedModByName` ou
  `LookupLoadedLightModByName`); vazio = sempre. Log `Compat: <nome> ativo por '<plugin>' (N exclusões)` /
  `inativo`.
- Listas somam (união): `discovery.exclude`, `conquest.exclude`, `conquest.excludePlugins` — aceita também
  `excludeFromPlugins` (nome do ADR 0042), já que a M5 usou `excludePlugins` no `settings.json`.
- Escalares só valem se o `settings.json` não os definiu: `conquest.skipCivilWarForts` e o novo
  `ai.skipCivilWarForts`.
- **Forte da Guerra Civil** = local com a keyword `LocTypeMilitaryFort` (resolvida pelo EditorID, como na
  descoberta). Aproximação: sem detectar as quests da Guerra Civil (EditorIDs de quests não ficam na memória sem
  outros mods); todos os fortes militares vanilla entram.
- **IA:** `ai.respectConquestExclusions` (padrão ligado) faz a IA pular tudo que a conquista exclui
  (settings + patches); `ai.skipCivilWarForts` pula os fortes militares. Calculado uma vez no load para os
  territórios estáticos (lookup O(1) no tick); claims são checados por id.
- O patch `civil-war-mods` passa a ligar também `ai.skipCivilWarForts`.
- `territories` condicional dentro do Compat (item 6 do ADR 0042) continua fora.

## Consequências
- Instalar o patch `civil-war-mods` agora muda o jogo (se algum plugin listado estiver carregado).
- Os nomes de plugin do patch continuam "a verificar" (ADR 0042).
