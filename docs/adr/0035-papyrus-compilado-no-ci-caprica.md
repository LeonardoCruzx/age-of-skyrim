# 0035. Scripts Papyrus compilados no CI com Caprica e stubs próprios

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M9/M10)

## Contexto
O compilador oficial do Papyrus vem com o Creation Kit e não pode ser redistribuído; os fontes vanilla
(`Form.psc`, `Quest.psc`...) e o código do SkyUI também não (o repositório do SkyUI não declara licença).
Ainda assim os `.pex` precisam sair no artefato do CI.

## Decisão
- **Caprica v0.3.0** (MIT, github.com/Orvid/Caprica, suporta `--game skyrim`), baixado no CI de URL fixa com
  **SHA-256 conferido**, extraído com o 7-Zip do runner. Roda antes do build C++ para falhar rápido.
- Fontes em `data/Scripts/Source/` (vão no pacote, como é costume); `.pex` em `Scripts/` no artefato.
- **Stubs próprios** em `tools/papyrus/stubs/` (escritos à mão, só as declarações usadas): `Form`, `Quest`,
  `ObjectReference`, `Location`, `SKI_QuestBase`, `SKI_ConfigBase`. Só servem para importar tipos; nunca são
  empacotados — em jogo valem os `.pex` do Skyrim e do SkyUI.
- Arquivo de flags próprio `tools/papyrus/AgeOfSkyrim_Flags.flg` (`Hidden` = bit 0, `Conditional` = bit 1,
  mesmos bits do jogo).
- O passo falha se o Caprica retornar erro ou se faltar algum `.pex` (anotação `::error` com o fim da saída).
- `AgeOfSkyrim.psc` é `Hidden` (sem `Native` no cabeçalho, como os scripts de plugins SKSE): só funções
  `Global Native`, ligadas pelo nome da classe.

## Consequências
- Build reproduzível sem CK, sem nada proprietário no repositório.
- Caprica é mais rígido que o compilador oficial (ex.: `None` não vira `Bool`/`String`), o que é aceitável.
- Se o Caprica sumir do GitHub, o passo falha de forma clara; alternativa: compilar localmente com o CK.
