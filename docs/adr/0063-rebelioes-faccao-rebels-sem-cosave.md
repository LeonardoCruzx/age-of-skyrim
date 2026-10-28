# 0063. Rebeliões: facção `rebels` fixa, aviso no mapa, estado transitório sem cosave

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M26)

## Contexto
Território com estabilidade baixa por tempo demais deve poder se rebelar (virar neutro ou de uma facção de
rebeldes), com aviso antecipado e no mapa tático (M11). Faltava decidir: (1) como criar a facção "rebels" sem
ESP/Creation Kit e sem tocar `Core/Factions`; (2) se as horas seguidas instável persistem no cosave.

## Decisão
- **Facção "rebels" via dados, não via API nova:** `Factions::Load` já lê `Factions/*.json` (extensível por
  mods) e exige que o `"style"` de cada facção esteja declarado em algum `Territories/*.json`. Em vez de abrir
  uma função de criação em runtime em `Core/Factions` (fora do escopo desta missão, que deve evitar
  `src/Realm/*` e tocar o mínimo em `src/Core/*`), a missão **entrega os dados**:
  `data/.../Factions/rebels.json` (facção `rebels`, não-jogador, sem `"aggression"`/`"start"`) +
  `data/.../Territories/rebels.json` (só declara o estilo `"rebels"`). `Stability::Load` resolve
  `Factions::Get().Find(MakeId("rebels"))`; ausente (ex.: um patch de compatibilidade remove o arquivo) = aviso
  no log e a rebelião cai para `kNeutral` mesmo com `stability.rebelFaction: true` — nunca crash.
- **Efeito colateral aceito:** qualquer facção pré-definida não-jogador conta como "de IA" para `Ai::Load`
  (`aiFactions_.insert` para todo `!f.player`, já era assim antes desta missão). Um território que os rebeldes
  tomarem pode, depois, se defender/expandir pelas regras normais da M16 — não é um bug, é tratado como um
  pequeno reino emergente. Sem território nenhum, os rebeldes nunca são `origin` de um ataque (`Ai::Strategic`
  exige um território já do atacante), então não existem "no mapa" antes da primeira rebelião de fato.
- **`SetTerritoryOwner` puro, sem `Conquest::AddListener`:** rebelião não passa pelo fluxo de "conquista do
  jogador" (`Conquest::ConquerForPlayer`), então não dispara `Diplomacy`'s `warOnConquest` nem vira uma "conquista"
  no sentido da M5. É uma troca de dono silenciosa do ponto de vista diplomático — suficiente para o objetivo
  (o território fica disputável de novo) sem acoplar a mais um sistema em paralelo.
- **Sem cosave para o estado transitório** (horas seguidas instável por território, aviso já emitido, abalo de
  execuções por facção): tudo fica só em memória (`Stability` não abre nenhum registro). Justificativa de
  custo/benefício: o pior caso de perder esse estado num load (meio de uma sequência instável) é atrasar no
  máximo `stability.graceHours` a próxima rebelião possível — sem efeito sobre donos de território, economia ou
  qualquer dado realmente persistido; versus o custo de manter outro formato de registro versionado para dados
  que se recalculam sozinhos a cada tick. Documentado aqui por pedido explícito da missão ("decida e documente").

## Consequências
- `Stability::Clear()` é chamado em `OnRevert` (novo jogo / antes de carregar outro save) para não vazar o
  estado em memória de uma sessão para a seguinte.
- Camada `"stability"` no mapa tático (`WarMap::AddProvider`, `src/Main.cpp`) expõe `score` e `unstableHours`
  por território, no mesmo padrão de `"garrison"`/`"upgrades"`/`"threats"`.
