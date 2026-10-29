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
- **M8 — Free Claim:** estandarte fincado por raycast e alinhado ao terreno, limites em `settings.json → claim`;
  cosave `CLAM` v2 (claim vira território dinâmico da facção, migra saves v1), debug F3.
- **M5 — Conquista por local limpo:** `TESLocationClearedEvent`/cadeia de locais → território; facção padrão
  criada na 1ª conquista; exclusões em `settings.json → conquest` (mods de guerra civil); debug F4.
- **M6 — Sede da facção:** candidatas em `Seats/*.json` (regra automática por província + lista + exclusões),
  pergunta ao conquistar, baú de estoque e mesa de guerra criados em runtime (`Placement`), mudança de sede
  transfere o baú; cosave `SEAT` v1; estoque da sede ligado ao F7/F8.
- **M9 — API pública:** interface C++ versionada (`include/AgeOfSkyrimAPI.h`, mensagem SKSE 'AOSI' + export
  `AoS_GetInterface`), eventos para C++ e Papyrus (`AoS_*`), natives em `AgeOfSkyrim.psc` compilado no CI (Caprica).
- **M10 — MCM:** menu do SkyUI (opcional) gerado como casca fina sobre os natives — facção, territórios, sede e
  estoque, debug; `AgeOfSkyrim.esp` (ESL) gerado por script, sem Creation Kit.
- **M18 — Governo e doutrina:** doutrinas/cargos/governadores em `Doctrines/*.json`, custo e espera na troca;
  cosave `GOVT` v1; debug NumPad1–4.
- **M19 — População:** população numérica por território (crescimento/fome), fonte de recrutas e multiplicador
  de produção; cosave `POPL` v1 compacto.
- **M22 — Títulos:** títulos por marcos (territórios, províncias, cidades, população), desbloqueios numéricos,
  anúncio na tela; cosave `TITL` v1.
- **M12 — Economia:** tick por hora de jogo disparado por eventos baratos (sono/espera/menus/célula), produção
  por tipo de território em `Economy.json`, depósito no baú da sede (pendente até existir sede), consumo e déficit,
  cosave `ECON` v1, debug F6.
- **M13 — Rotas de comércio:** rotas entre territórios da mesma facção com renda por distância/tipo e corte por
  território inimigo, cosave `ROUT` v1, debug F7.
- **M11 — Mesa de guerra + mapa tático:** a mesa da sede abre o mapa (view PrismaUI opcional em SVG procedural:
  tipos, donos, províncias, vizinhança, rotas ativas/cortadas, painel com estoque e camadas; criar/remover rota e
  marcar alvo) ou menus vanilla sem PrismaUI; camadas por provedor para outros módulos, alvos no cosave `WMAP` v1,
  dados de exemplo e teste do schema, debug NumPad9.
- **M14 — Guarnição:** guarnição numérica por território (`Garrison.json`, cosave `GARR` v1) que só vira atores
  perto do jogador, em lotes por frame e com teto global (`settings.json → garrison`); tropas vanilla por estilo
  com outfit aplicado pelo DLL; manutenção por soldado, reposição e deserção no tick da economia; debug NumPad5.
- **M15 — Melhorias de territórios:** `Upgrades.json` (muralhas, quartel, torre de vigia, armazém, mercado,
  forja; I–III) com custo em itens do baú da sede e tempo de obra, status por território (defesa, renda,
  guarnição máx., capacidade, visão), objetos visuais por nível, cosave `UPGR` v1, debug NumPad6.
- **M16 — Facções de IA e contra-ataques:** avaliação estratégica a cada N horas de jogo no tick da economia,
  ataques numéricos pagos com o saldo de ouro (cosave `WARS` v1), contra-ataque com sede ocupada (depósito
  suspenso), donos iniciais e agressão por facção, provedor de defesa para M14/M15, gancho da batalha real (M21),
  loader de `Compat/*.json` com exclusões da IA e dos fortes da Guerra Civil, debug NumPad7.
- **M17 — Diplomacia:** guerra/paz/aliança/trégua entre facções com padrões em `Factions/*.json` (cosave `DIPL` v1),
  propostas de paz/aliança com aceitação por poder, guerra ao tomar território, rotas aceitam paz/aliança,
  `AreHostile` para guardas, debug NumPad8.
- **M20 — Exército e recrutamento:** tropas de campo em `Troops.json` (mesma escolha de `ActorBase`/outfit da
  guarnição), `Army::Recruit` consumindo ouro/equipamento do baú/população com fila de treino, exércitos
  ABSTRATOS por território (cosave `ARMY` v1), marcha entre vizinhos, oficiais (bônus simples), escolta com teto
  global, manutenção e deserção por déficit, gancho de confronto para a M21, debug NumPad0/NumPad.
- **M21 — Batalhas em tempo real:** materialização de ataques da IA ou confrontos de exércitos perto do jogador,
  teto GLOBAL de atores próprio (`battle.maxActors`), spawn em lotes por frame com reforços em ondas, facções de
  combate criadas em runtime (nunca vanilla), formações simples nos marcadores do território, resolução numérica
  reaproveitada fora do alcance do jogador, cosave `BATL` v1 (só limpeza de órfãos), gancho de captura opcional
  para a M25, debug NumPad+.
- **M28 — Infra de release:** workflow de release em tag `v*`, instalador FOMOD com patches opcionais, patch de
  compatibilidade com mods de guerra civil (lido pelo DLL desde a M16), relatório de performance
  do log (`tools/perf_report.py`).
- **M24 — Governo local:** ao conquistar a capital de uma província (cidade de maior população, senão o
  território de maior população), oferece um governador seu com presença física — NPC não-único do `Skyrim.esm`
  (escolha automática ou `settings.json → jarl`), perto do marcador da cidade, **sem tocar o jarl vanilla**
  (ADR 0005/0056); reaproveita `Government::AssignGovernor` (M18) para a maioria/bônus. Ativar o NPC mostra o
  status da província (MessageBox só-texto, sem dublagem). Cosave próprio `JARL` v1. Debug NumPad-.
- **M25 — Lei e prisioneiros de guerra:** `Law::Prisoners`/`Capacity` por facção (sem ator, cosave `LAWS` v1),
  ganho por combates da IA (`Ai::Resolve` chama `CaptureFromCombat`) ou por debug (F1); resgate (ouro, F2) e
  execução (F12); prisão como melhoria (`Upgrades.json → "prison"`, `Stat::kPrisonCapacity`); facção de crime
  vanilla do Hold (bounty) zerada ao trocar de dono, desligado por padrão (`law.touchCrimeFaction`).
- **M26 — Ordem pública e rebeliões:** `Realm::Modifier::Stability` (doutrina/governador/guarnição vs.
  população/déficit), rebelião por território instável por tempo demais (facção `rebels` ou neutro), aviso na
  tela e camada nova no mapa tático, debug NumPad. (decimal).
- **M27 — Domínio de Skyrim:** marco permanente "Dominador(a) de Skyrim" ao controlar a maioria de
  `realm.dominationThreshold` (padrão 1.0 = todas) das províncias existentes; avaliado dentro do mesmo
  `Realm::Reevaluate` que já reavalia títulos, sem polling; vitória suave — sem travar nada. Cosave `DOMN` v1,
  exposto por `Realm::HasDominated`, native Papyrus e a página "Reino" do MCM.

### Corrigido
- `docs/adr/0042`/`docs/ROADMAP.md`: marcavam a leitura de `Compat/*.json` no DLL como pendente; já foi feita
  junto da M16 (`Config::LoadCompat`) — só a documentação estava desatualizada.
- **Crash ao abrir o jogo em Skyrim ≥ 1.7.x** — dois bugs em cadeia no CommonLibSSE-NG vendorizado, ambos
  corrigidos por patch aplicado no CI (`tools/vendor-patches/`), sem alterar o pin do submódulo (ADR 0066):
  1. `REL/ID.h(219): failed to open address library file` — classificava qualquer versão do jogo fora de
     `1.6.x`/`1.4.x` como SE pré-Anniversary Edition, abrindo o arquivo errado do Address Library.
  2. `REL/ID.h(257): Unsupported address library format: 5` — o Address Library mais recente gera o
     `versionlib-*.bin` num formato novo que esta cópia não lê; agora prefere o companheiro legado
     `versionl2b-*.bin` (formato antigo) quando ele existe.

### Observações
- Roteiro de teste único e consolidado para as 28 missões em [TESTES.md](TESTES.md) (ADR 0021); ainda não
  executado no jogo.
