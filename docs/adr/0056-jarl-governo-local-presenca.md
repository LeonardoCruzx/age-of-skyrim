# 0056. Governo local (M24): capital da província, presença do governador, sem tocar o jarl vanilla

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M24)

## Contexto
A M18 ([ADR 0036](0036-governo-doutrina-cargos.md)) já tem `Government::AssignGovernor` (bônus numérico por
província, exige maioria). A M24 pede presença física na capital da província: um NPC nosso perto do marcador,
oferta ao conquistar, reversão ao perder a capital, e diálogo só texto. O comparativo com o Conquest of Skyrim
(`docs/COMPARATIVO-CONQUEST-OF-SKYRIM.md`, seção 2) lembra duas lições diretas: nunca editar NPCs/pacotes/aliases
de quests vanilla (a Guerra Civil usa jarls como atores de quest, `ISSUE-QST-03/04`) e nunca reaproveitar NPCs
vanilla com pacotes/aliases presos (`ISSUE-GAME-03`) — por isso o NPC do governador é sempre um ator *nosso*,
nunca o jarl original.

## Decisões

### Capital da província
Sem uma forma genérica e segura de achar "o NPC do jarl" por local (varia por Hold, nem todo mapa de mod a tem),
a capital é a heurística: **o território do tipo `city` com maior população (`Realm::Population::Of`) na
província**; se a província não tem nenhum território `city` (provinces só de fortes/torres, ex. de mods de
territórios), cai para o território de **maior população entre todos os da província** (log avisa a heurística
usada). Empate de população: desempate determinístico pela chave do território (ordem alfabética). Calculado em
`Jarl::CapitalOf`, O(territórios da província) — mesma classe de custo de `Realm::ComputeStats`, chamado só em
eventos (conquista), nunca por frame.

### Zero edição do jarl vanilla (ADR 0005)
O NPC do jarl original **não é tocado de forma alguma** pelo nosso código: não é desabilitado, não tem pacotes
ou aliases alterados, nem é referenciado por FormID. É tecnicamente arriscado desabilitar um ator amarrado a
aliases de quest vanilla (Guerra Civil, Companheiros, rádio de diálogo da corte) sem saber se outro mod depende
dele estar ativo — poderia travar uma quest vanilla ou de outro mod. Por isso, diferente do rascunho inicial do
`docs/ROADMAP.md` (M24: "o jarl original é desabilitado"), a decisão final é **não desabilitar**: o governador
nosso aparece **ao lado**, como uma segunda presença visível perto do marcador da cidade. `docs/ROADMAP.md` foi
atualizado para refletir esta decisão (mais conservadora que a redação original). Se no futuro aparecer uma forma
seguramente detectável de saber que nenhuma quest ativa depende do jarl (ex.: só depois de `PlayerOwnsSettlement`
o jogo já o colocar institucionalmente em segundo plano), isso fica como TODO — não implementado agora.

### NPC do governador: base vanilla + outfit pelo DLL
Mesmo padrão de `Seat::AutoChest`/`AutoWarTable` (src/World/Seat.cpp) e `Garrison::AutoSoldier`
(src/Sim/Garrison.cpp): escolha automática em runtime de um `TESNPC` do `Skyrim.esm`, **não único**
(`IsUnique() == false` — isso também evita acidentalmente reusar o próprio jarl vanilla, que é sempre único),
de raça jogável, com nome, cujo outfit padrão bate com uma dica de modelo (`settings.json → jarl.autoHint`,
padrão `"noble"`). `settings.json → jarl.actor`/`jarl.outfit` permitem um FormID/EditorID explícito (como
`seat.chest`/`seat.warTable`). FaceGen pronto (BSA vanilla) evita o problema "ator sem malha" do comparativo
(`ISSUE-CTD-03`). Outfit vestido pelo DLL ao criar e reconferido no próximo frame (lição "tropas peladas",
`ISSUE-GAME-01`, mesmo padrão do `Garrison::EnsureOutfit`).

**Limitação conhecida (não resolvida agora):** sem Creation Kit não é possível atribuir um pacote de IA próprio
ao NPC; ele usa o pacote padrão da base escolhida (tipicamente um sandbox genérico). Existe o risco de o NPC se
mover para fora do raio esperado se a base tiver um pacote de viagem vinculado a outro local. Teste no jogo:
confirmar que o governador permanece perto do marcador por tempo suficiente. Se for um problema, o TODO é
restringir a escolha automática a bases sem pacotes de viagem conhecidos (heurística adicional).

### Posição: perto do marcador, não do jogador
Diferente da Sede (M6, que nasce relativa ao jogador por falta de coordenadas curadas — ADR 0023), aqui a
posição é relativa ao **marcador da capital** (`Territory::location->worldLocMarker`, ou a primeira bandeira,
mesmo padrão de `Garrison::FindAnchor`): funciona mesmo que o jogador não esteja exatamente no marcador no
momento da oferta (ex.: hotkey de debug a alguma distância), desde que a célula esteja carregada.

### Diálogo só texto (ADR 0055) e limite técnico da ativação
Ativar o NPC mostra um `Prompt::Ask` (MessageBox, um botão "OK") com o status da província: população somada
dos territórios da província (`Realm::Population::Of`) e um texto fixo de "estabilidade" como ponto de extensão
(M26 ainda não existe). **Limitação não verificada:** o `TESActivateEvent` do CommonLibSSE-NG é só notificação —
não há como cancelar a ativação padrão do jogo. É possível que a ativação também dispare uma saudação genérica
de voz da base NPC escolhida (se ela tiver linhas de "Hello" vinculadas ao tipo de voz) antes do nosso MessageBox
aparecer. Sem dublagem própria (ADR 0055) e sem CK para criar tópicos de diálogo "silenciosos", isso não tem
solução completa nesta missão; peço teste no jogo para confirmar que não há conflito visual incômodo.

### Governador sem maioria / sem NPC
`Jarl::Place` chama `Government::AssignGovernor` **antes** de criar o NPC (reserva a vaga com `npc=0`); se a
facção não tem maioria (`Result::kNoMajority`) ou não há vaga (`kNoSlot`), nada é criado e o jogador é avisado —
sem NPC "fantasma" numa vaga inválida. Se a vaga é concedida mas a célula da capital não está carregada, a vaga
de governador fica sem NPC (`npc=0`, igual ao padrão já usado por `AssignOfficer`/`AssignGovernor` desde a M18)
até a próxima oferta (debug) ou re-conquista.

### Cosave: registro próprio `JARL`, não o `GOVT`
Em vez de estender o formato já fechado do `GOVT` v1 (M18, Government.cpp), a presença fica num registro
**novo e isolado**: `JARL` v1 — `u32 count`, depois `count × { Id província, FormID npc }`. Motivos: (1) não
arrisca quebrar a leitura do `GOVT` de saves já em teste de outras missões da Fase 4/5 em paralelo (menor
risco de conflito de merge, ADR 0055); (2) a presença (NPC) e o cargo numérico (`Government`) têm ciclos de
vida relacionados mas não idênticos — perder a célula carregada no load não deveria apagar o registro de
governador numérico, só a presença.

### Remoção da presença
Dois gatilhos, ambos aditivos (sem alterar a lógica existente de `Government`/`Titles`):
1. **Perder a capital:** o listener de conquista de `Jarl` (`Conquest::AddListener`, mesmo padrão de
   `Seat`/`Garrison`) detecta troca de dono da própria capital e remove a presença, mesmo que a facção ainda
   tenha maioria na província (o NPC está fisicamente no local perdido).
2. **Perder a maioria:** `Realm::Reevaluate` (src/Realm/Realm.cpp) já chama `Government::ValidateGovernors` e
   recebe a lista de províncias cujo governador saiu; uma linha nova chama `Jarl::OnGovernorsLost(lost)` para
   remover a presença dessas províncias também (cobre perder maioria sem perder a capital em si).

## Consequências
- `src/Realm/Jarl.h/.cpp` novo; `Config` ganha `settings.json → jarl` e `debug.jarlHotkey` (NumPad0); `Main.cpp`
  ganha `Jarl::Get().Load()` e o despacho do registro `JARL` no cosave.
- `docs/ROADMAP.md` (M24) foi ajustado: não desabilitamos o jarl vanilla (mais conservador que o rascunho).
- M25/M26 (outro agente, `src/Realm/Law*`/`Stability*`) não são tocados; a ordem pública futura pode um dia
  querer mostrar "estabilidade" no mesmo MessageBox — o ponto de extensão já está comentado no código.
