# 0041. Instalador FOMOD: núcleo obrigatório + patches opcionais, idioma automático

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M28)

## Contexto
A M28 pede um instalador FOMOD com patches selecionáveis. Os jogadores usam MO2 ou Vortex, que leem
`fomod/info.xml` + `fomod/ModuleConfig.xml`. O mod já é traduzido em en/pt-br com escolha automática pelo idioma
do jogo (`settings.json → "language": "auto"`, ADR 0014).

## Decisão
- Layout do zip (montado por `tools/package_release.py`):
  - `fomod/info.xml` (versão reescrita com a da tag) e `fomod/ModuleConfig.xml` (estático, no repositório);
  - `00 Core/` = conteúdo inteiro do artefato do CI (`SKSE/Plugins/AgeOfSkyrim.dll`, PDB quando existir, JSONs e
    o que o build passar a gerar, como scripts/ESP) — em `requiredInstallFiles`;
  - `patch_<nome>/` = cada `patches/<nome>/` do repositório, sem arquivos `.md`, espelhando `Data/`.
- Um único passo "Patches de compatibilidade" (`SelectAny`, todos `Optional`). Nada de `Recommended` por
  dependência de plugin enquanto os nomes de plugin não forem conferidos (ADR 0042).
- Pastas sem separador (`patch_<nome>`, não `patches/<nome>`) para não depender de como cada gerenciador trata
  `/` e `\` em `source`.
- O PDB vai no núcleo quando o artefato o tiver (ajuda crash loggers; ~MBs); sem PDB o pacote sai igual, sem ele.
- **Sem escolha de idioma no instalador:** o FOMOD não edita arquivos, só copia; oferecer idioma exigiria duas
  cópias do `settings.json` (que muda a cada missão) e quebraria a escolha automática. O padrão de release é
  `"language": "auto"`; `package_release.py` avisa se o artefato vier com outro valor ou com `debug.enabled`.
- Textos do instalador em inglês e português no mesmo campo (FOMOD não tem localização); não passam pelo I18n do
  DLL, que só cobre texto em jogo.
- Validação automática (`tests/test_fomod.py`, `package_release.py --check`): XML bem-formado, núcleo obrigatório,
  cada `patches/<nome>` com um `<plugin>` correspondente e vice-versa, toda `source` presente no pacote, JSON dos
  patches válido.

## Consequências
- Adicionar patch = pasta em `patches/` + um `<plugin>` no `ModuleConfig.xml`; o teste pega o esquecimento.
- Instalação manual (sem gerenciador) = copiar `00 Core/` para `Data/` (e os `patch_*` desejados).
- Trocar o idioma continua sendo editar `settings.json` (documentado na página do Nexus).
