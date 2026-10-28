# 0065. Batalhas em tempo real: materialização, teto próprio e facções de combate próprias (M21)

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica

## Contexto
A M21 (comparativo seção 2/3, [ADR 0031](0031-exercito-governo-populacao-titulos.md)) materializa os ataques da
IA (M16) e os confrontos de exército (M20) perto do jogador. As lições do comparativo (seção 2) valem todas:
teto global de atores e lotes por frame (`ISSUE-CTD-01/04`), nunca editar navmesh/células (`ISSUE-CTD-02`),
FaceGen vanilla pronto (`ISSUE-CTD-03`), outfit pelo DLL (`ISSUE-GAME-01`) e pacotes próprios, nunca NPC vanilla
com aliases/pacotes (`ISSUE-GAME-03`).

## Decisão
- **Dois ganchos, um só caminho de materialização:** `Battle` registra `Ai::SetNearPlayerHandler` (M16) e
  `Army::SetClashHandler` (M20) SÓ se `battle.realTime` estiver ligado; sem registro, Ai/Army caem sozinhos na
  resolução numérica que já existe — `Battle` nunca duplica essa lógica, só decide SE materializa atores. Ao
  final (vitória/derrota por baixas ou abandono pelo jogador), `Battle` chama de volta `Ai::ResolveAttack`
  (reaproveita `Ai::Resolve`, não reimplementa) ou `Army::ApplyLosses`/`Disband`.
- **Teto GLOBAL de atores PRÓPRIO:** `battle.maxActors`, contado em `aliveTotal_` (atomic), SEPARADO de
  `garrison.maxActors` — uma guerra não compete por orçamento de atores com a vida numérica das guarnições.
  Criação em lotes por frame: fila (`queue_`) + `ProcessQueue`/`SpawnOne`, mesmo padrão de
  `Garrison::ProcessQueue`/`SpawnOne` ([ADR 0043](0043-guarnicao-numerica-materializada.md)), com `ScopedTimer`
  por chamada e `logger::warn` se o lote passar de `battle.maxFrameMs` (meta ~2 ms/frame).
- **Reforços em ondas:** cada lado guarda `totalCount` (força abstrata a representar), `spawned` (reservado
  para a fila, nunca desce) e `alive`/`casualties` (reais). `OnActorDeath` incrementa `casualties` e, se
  `spawned < totalCount`, enfileira +1 reforço; a batalha só conclui quando `casualties >= totalCount` de um
  lado (ou o jogador some da área).
- **Atores:** `Troops::TroopFor` (M20) escolhe o `ActorBase` (FaceGen vanilla pronto) e o outfit, aplicado pelo
  DLL em `SpawnOne` e reconferido no frame seguinte ("tropas peladas", mesma lição de `Garrison::EnsureOutfit`).
  Posições: 2-3 posições relativas por lado (linha/defesa), deslocadas do marcador (`FindAnchor`, igual à
  guarnição: `worldLocMarker` do território, senão uma bandeira) — SEM navmesh novo, os atores caem no navmesh
  vanilla do local, exatamente como a guarnição já faz.
- **Facções de combate PRÓPRIAS:** duas `TESFaction` criadas em runtime uma única vez
  (`IFormFactory::GetConcreteFormFactoryByType<TESFaction>()->Create()`) e marcadas mutuamente hostis
  (`TESFaction::SetEnemy`), reaproveitadas por todas as instâncias (na prática só uma por vez fica perto do
  jogador). NUNCA as facções vanilla (Guerra Civil etc.) — evita depender de quests da Guerra Civil que podem
  estar excluídas (ADR 0042) e evita qualquer edição de registro vanilla.
  **Suposição não verificada:** o comportamento de `IFormFactory::Create()`/`SetEnemy()` em runtime (sem ESP)
  não foi testado no jogo; se os atores não entrarem em combate automaticamente, o próximo passo seria forçar
  via pacote de combate próprio (ainda sem NPC/pacote customizado nesta missão).
- **Batalha NÃO sobrevive ao save** (o comentário de `Ai::LoadRecord`, M16, já previa isso): nenhuma batalha
  ativa é persistida com estado; só um registro mínimo **`BATL` v1** (lista de atores vivos no momento do save)
  para o `OnPostLoad` apagar soldados temporários órfãos — mesma lição `stale_` de `Garrison::OnPostLoad`. No
  próximo tick, o ataque (Ai) ou os exércitos (Army) envolvidos se reavaliam e, se ainda relevante, materializam
  de novo.
- **Abandono:** o jogador saindo do raio (`battle.radius`) do marcador conclui a batalha pela força abstrata
  RESTANTE de cada lado (± `battle.randomness`, mesmo sorteio reprodutível da IA) em vez de travar ou inventar
  um resultado fixo.
- **Ponto de integração opcional para a M25:** `Battle::SetCaptureHandler(fn)`, chamado ao fim de uma batalha
  real (não na de debug) com a facção derrotada, o território e as baixas dela — para quem quiser transformar
  baixas em prisioneiros. Sem handler registrado, nada muda; a M21 não depende da M25.
- Debug (`debug.battleTestHotkey`, NumPad+ / DIK 0x4E): dois grupos pequenos (facção do jogador x a primeira
  facção de IA) se enfrentam perto do jogador, sem depender de um `Ai::Attack` ou `Army` reais.

## Consequências
- Nenhum custo de atores fora do raio da batalha; o teto próprio protege o desempenho mesmo com guarnições e
  escoltas grandes por perto.
- Formação é só um espalhamento simples (sem curar coordenadas por território); aceito como MVP visual.
- Sem cerco/arma de cerco (fora do roadmap, ADR 0055) — toda batalha é de campo aberto, nos marcadores do local.
