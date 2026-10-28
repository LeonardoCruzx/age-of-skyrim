# 0033. MCM do SkyUI como casca fina, com textos vindos do DLL

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M10)

## Contexto
O ADR 0010 decidiu um MCM fino primeiro. O mod tem dois idiomas (ADR 0014) escolhidos no `settings.json`, mas o
SkyUI traduz tokens `$CHAVE` pelo `sLanguage` do jogo — e o Skyrim SE não tem idioma PT-BR oficial (quem joga em
português usa o jogo em inglês).

## Decisão
- `AgeOfSkyrimMCM.psc` (`extends SKI_ConfigBase`) só monta a tela e chama natives (`AgeOfSkyrim.psc`). Páginas:
  **Facção** (criar/renomear com caixa de texto, estilo por menu, cor), **Territórios** (40 por página, dono;
  com debug ligado o dono é trocado por menu), **Sede e Estoque** (sede via gancho da M6 → "sem sede";
  inventário do jogador por categoria), **Economia e Governo** (texto de "em breve"), **Depuração** (versão,
  atalhos, autoteste/estoque/claim).
- **Textos:** o MCM pede cada texto ao DLL (`AgeOfSkyrim.Tr/Tr1/Tr2("mcm.…")`), que usa as mesmas traduções
  `Translations/*.json` e o idioma do `settings.json`. As páginas são recriadas a cada abertura (`OnConfigOpen`),
  então trocar o idioma vale sem novo jogo. Codificação: a mesma opção `textEncoding` do HUD.
- **Tokens do SkyUI** só para o que precisa funcionar sem o DLL: nome do mod na lista (`$AOS_ModName`) e o aviso
  "DLL não carregado" (`$AOS_DllMissing`). Chaves `skyui.*` dos JSON → `Interface/Translations/AgeOfSkyrim_<IDIOMA>.txt`
  (UTF-16 LE com BOM) gerados no CI por `tools/gen_skyui_translations.py`. ENGLISH = en; demais idiomas oficiais
  recebem cópia em inglês; `PORTUGUESE` e `BRAZILIAN` (usados por traduções da comunidade) recebem pt-br.
- `tools/check_translations.py` confere também as chaves e a quantidade de `{}` usadas nos `.psc`.
- **SkyUI é opcional:** sem ele os scripts do MCM não carregam (aviso no log do Papyrus) e o mod funciona igual
  (hotkeys, API). Caixa de texto (`AddInputOption`) exige **SkyUI 5.x** (API do MCM ≥ 4).
- Quest do MCM no ESP gerado sem CK ([ADR 0034](0034-esp-gerado-sem-creation-kit.md)); scripts compilados no CI
  ([ADR 0035](0035-papyrus-compilado-no-ci-caprica.md)).

## Consequências
- Nenhuma lógica no Papyrus: trocar para PrismaUI (M11) não muda o DLL.
- Suposições não testadas no jogo: assinaturas do SkyUI 5.x (`AddInputOption`, `OnOptionInputAccept`,
  `SetInputDialogStartText`) escritas de memória nos stubs; acentos via strings do Papyrus aparecem certos em UTF-8.
