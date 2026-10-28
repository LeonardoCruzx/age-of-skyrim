# 0044. Tropas da guarnição: bases vanilla, outfit pelo DLL e facções de combate

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica

## Contexto
Sem ESP/Creation Kit não há NPCs nem leveled lists nossos. Lições obrigatórias
([comparativo](../COMPARATIVO-CONQUEST-OF-SKYRIM.md) seção 2): FaceGen pronto, outfit aplicado pelo DLL ("tropas
peladas"), nada de NPC vanilla com aliases/pacotes de quest, zero edição de registros vanilla.

## Decisão
- **`Garrison.json → troops`**, por facção ou estilo de bandeira, com `"default"` como reserva (inclui a facção do
  jogador). Outros mods: `Garrison/*.json`.
- **Base:** `actor` = lista de candidatos (`"Plugin.esp|0xID"` de `NPC_`/`LVLN` ou EditorID). EditorID de NPC só
  existe em memória com po3 Tweaks, então os candidatos do arquivo padrão (palpites, não verificados) podem falhar.
  Reserva **garantida**: `autoArmorHint` — o DLL escolhe o primeiro NPC **genérico** (não único) do `Skyrim.esm`,
  de raça jogável, com nome, cujo outfit padrão (inclusive dentro de leveled items, 2 níveis) tem armadura com o
  texto no caminho do modelo (`imperial`, `stormcloak`). NPC do `Skyrim.esm` = FaceGen pronto no BSA; template,
  nunca um NPC com alias de quest. O log mostra cada candidato tentado e o escolhido.
- **Outfit:** `outfit` (candidatos `OTFT`) ou o outfit padrão do NPC. Aplicado ao criar, de novo no frame seguinte e
  a cada `attach` da célula (`HasOutfitItems`/`AddWornOutfit`). A base vanilla **nunca** é alterada: só base
  temporária de leveled actor (`0xFF…`) recebe `SetDefaultOutfit`.
- **Facções de combate:** soldados de facção do jogador entram na `PlayerFaction` (`Skyrim.esm|0x000DB1`, aliados do
  jogador) só na referência criada. Soldados de IA mantêm as facções vanilla do template (ex.: facção da Legião).
  `factions`/`removeFactions` no JSON ajustam por tropa. Não criamos `TESFaction` em runtime agora: relações entre
  facções do mod ficam para M17/M21.
- **Pacotes:** os do template genérico (sem quest/alias); `EvaluatePackage()` ao criar. Pacote próprio (sandbox no
  marcador) entra quando houver como criá-lo sem ESP.

## Consequências
- Funciona com qualquer load order; mods que trocam a armadura imperial mudam o NPC escolhido automaticamente (o log
  diz qual).
- Soldados do jogador com template imperial podem ser alvo dos Mantos da Tempestade (facção vanilla do template);
  `removeFactions` resolve quando o usuário souber o FormID.
- Candidatos com FormID curado substituem a heurística assim que forem conferidos no jogo.
