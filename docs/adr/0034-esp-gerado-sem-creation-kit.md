# 0034. AgeOfSkyrim.esp gerado por script (sem Creation Kit)

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M10)

## Contexto
O MCM do SkyUI precisa de uma quest com o script anexado (VMAD) e um alias do jogador com
`SKI_PlayerLoadGameAlias`. O fluxo do projeto não usa o Creation Kit (ADR 0008) e binários não devem viver no git.

## Decisão
- `tools/build_esp.py` gera o `AgeOfSkyrim.esp` no CI (passo "Montar pacote"); `tests/test_build_esp.py` relê o
  arquivo com um parser independente e valida a estrutura.
- Conteúdo mínimo, só forms novos (zero edição de registros vanilla, ADR 0005):
  - `TES4`: flag **ESL** (0x200) e nada mais; `HEDR` versão **1.71**, contagem = records + grupos, próximo
    objeto 0x801; `CNAM`, `SNAM`; master `Skyrim.esm` (`MAST` + `DATA` u64 0). Versão de form **44** em todo record.
  - `GRUP` de topo `QUST` com `AoS_MCMQuest` = FormID `0x01000800` (índice 01 = o próprio plugin depois de
    Skyrim.esm; faixa ESL 0x800–0xFFF).
  - Subrecords na ordem do xEdit (TES5): `EDID`, `VMAD`, `DNAM` (12 bytes: flags `Start Game Enabled`,
    prioridade 0, tipo 0), `NEXT` (marcador), `ANAM` = 1, alias `ALST` 0 / `ALID` "PlayerAlias" / `FNAM` 0 /
    `ALFR` 0x00000014 (PlayerRef) / `ALED`.
  - `VMAD` v5, formato de objeto 2: script `AgeOfSkyrimMCM`; bloco de fragmentos de quest com versão 2,
    0 fragmentos e `fileName` vazio (sempre presente); 1 alias (objeto v2 = quest + alias 0) com
    `SKI_PlayerLoadGameAlias`. Sem propriedades.
- Para crescer (base objects da sede na M6 etc.): novos records em `RECORDS` com FormIDs 0x801+ e `NEXT_OBJECT_ID`.
- O jogador precisa **ativar o AgeOfSkyrim.esp** (ocupa um slot ESL/FE, não um slot normal).

## Consequências
- Formato conferido com UESP e com as definições do xEdit, mas **não testado no jogo**: confirmar no primeiro teste
  que o MCM aparece (log do Papyrus sem "Cannot open store for class" / erro de VMAD).
- Sem `.seq`: a quest não tem diálogo; Start Game Enabled basta para iniciar também em saves existentes.
- Sem SkyUI o ESP carrega normalmente; os scripts só faltam (aviso no log do Papyrus).
