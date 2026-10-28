# Changelog

Todas as mudanças relevantes do Age of Skyrim ficam aqui. Formato
[Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/), versões em [SemVer](https://semver.org/lang/pt-BR/).
O CI de release usa a seção `## [X.Y.Z]` da tag `vX.Y.Z` como notas do GitHub Release ([docs/RELEASE.md](docs/RELEASE.md)).

## [Não lançado]

### Adicionado
- **M1 — CI:** build do DLL no GitHub Actions (`windows-2022`, vcpkg com cache em disco), zip do mod como artefato,
  conferência das traduções en/pt-br.
- **M2 — Teste sem ESP:** objetos de teste criados em runtime (cosave `PLAC` v1), autoteste F11 com checagem de
  persistência (`TEST` v1), despacho central do cosave, tempo das operações no log (`[tempo]`).
- **M3 — Facções:** facções pré-definidas (`Factions/*.json`) e do jogador (cosave `FACT` v1); o dono de território
  passa a ser a facção (`TERR` v2, migra saves com v1).
- **M4 — Territórios do jogo:** territórios descobertos dos `BGSLocation` (vanilla e de outros mods) por keyword
  `LocType*`, com tipo, província, posição e vizinhos; `settings.json → discovery` (tipos, exclusões, vizinhos).
- **M7 — Estoque (núcleo):** categorias em `Resources.json` (por form, keyword e tipo), recontagem ao fechar baú
  observado, debug F8.
- **M8 — Free Claim (núcleo):** estandarte fincado por raycast e alinhado ao terreno, limites em
  `settings.json → claim`, cosave `CLAM` v1, debug F9.
- **M12 — Economia:** tick por hora de jogo disparado por eventos baratos (sono/espera/menus/célula), produção
  por tipo de território em `Economy.json`, depósito no baú da sede (pendente até existir sede), consumo e déficit,
  cosave `ECON` v1, debug F6.
- **M13 — Rotas de comércio:** rotas entre territórios da mesma facção com renda por distância/tipo e corte por
  território inimigo, cosave `ROUT` v1, debug F7.
- **M14 — Guarnição:** guarnição numérica por território (`Garrison.json`, cosave `GARR` v1) que só vira atores
  perto do jogador, em lotes por frame e com teto global (`settings.json → garrison`); tropas vanilla por estilo
  com outfit aplicado pelo DLL; manutenção por soldado, reposição e deserção no tick da economia; debug NumPad5.
- **M15 — Melhorias de territórios:** `Upgrades.json` (muralhas, quartel, torre de vigia, armazém, mercado,
  forja; I–III) com custo em itens do baú da sede e tempo de obra, status por território (defesa, renda,
  guarnição máx., capacidade, visão), objetos visuais por nível, cosave `UPGR` v1, debug NumPad6.
- **M23 — Infra de release:** workflow de release em tag `v*`, instalador FOMOD com patches opcionais, patch de
  compatibilidade com mods de guerra civil (formato definido, leitura no DLL pendente), relatório de performance
  do log (`tools/perf_report.py`).

### Observações
- Ainda não testado no jogo (roteiro único no fim, [ADR 0021](docs/adr/0021-implementar-tudo-teste-unico-no-fim.md)).
