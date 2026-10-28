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
- **M16 — Facções de IA e contra-ataques:** avaliação estratégica a cada N horas de jogo no tick da economia,
  ataques numéricos pagos com o saldo de ouro (cosave `WARS` v1), contra-ataque com sede ocupada (depósito
  suspenso), donos iniciais e agressão por facção, provedor de defesa para M14/M15, gancho da batalha real (M21),
  loader de `Compat/*.json` com exclusões da IA e dos fortes da Guerra Civil, debug NumPad7.
- **M17 — Diplomacia:** guerra/paz/aliança/trégua entre facções com padrões em `Factions/*.json` (cosave `DIPL` v1),
  propostas de paz/aliança com aceitação por poder, guerra ao tomar território, rotas aceitam paz/aliança,
  `AreHostile` para guardas, debug NumPad8.
- **M23 — Infra de release:** workflow de release em tag `v*`, instalador FOMOD com patches opcionais, patch de
  compatibilidade com mods de guerra civil (lido pelo DLL desde a M16), relatório de performance
  do log (`tools/perf_report.py`).

### Observações
- Ainda não testado no jogo (roteiro único no fim, [ADR 0021](docs/adr/0021-implementar-tudo-teste-unico-no-fim.md)).
