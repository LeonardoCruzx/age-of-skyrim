# 0014. Mod traduzido em inglês e português do Brasil

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Decisão do usuário

## Contexto
O público do Nexus é majoritariamente anglófono; o autor e parte do público falam PT-BR.

## Decisão
Nenhum texto de jogador fica no código. O DLL usa chaves em `Translations/en.json` e `pt-br.json` (UTF-8), com fallback para inglês; nomes nos JSONs de dados aceitam `"texto"` ou `{ "en": ..., "pt-br": ... }`. Idioma em `settings.json` (`language`: `en`/`pt-br`/`auto`); como o Skyrim não tem PT-BR oficial, `auto` cai em inglês. `textEncoding` (`utf8`/`cp1252`) cobre o que o HUD exibir. O CI falha se as chaves ou placeholders divergirem entre os idiomas. MCM e PrismaUI seguirão o mesmo esquema; página do Nexus e FOMOD nos dois idiomas. Logs técnicos continuam em português (uso de desenvolvimento).

## Consequências
- Toda string nova entra nos dois arquivos no mesmo commit (garantido pelo CI).
- A codificação do HUD é verificada no autoteste da M2.
