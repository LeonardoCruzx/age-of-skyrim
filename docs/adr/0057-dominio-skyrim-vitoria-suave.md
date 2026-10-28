# 0057. Domínio de Skyrim (M27): marco separado, não um título, avaliação dentro de Realm::Reevaluate

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M27)

## Contexto
A M27 ([ADR 0055](0055-dominio-suave-lei-ordem-jarls.md)) pede a vitória suave: ao controlar todas (ou uma
fração configurável) as províncias, a facção do jogador ganha o marco "Dominador(a) de Skyrim" e um aviso
único — sem travar nada. A M22 ([ADR 0038](0038-titulos-progressao.md)) já tem `Realm::Titles`, com marcos
absolutos (`territories`, `provinces`, `cities`, `population`).

## Decisão

### Marco separado, não um título de `Titles/*.json`
`TitleRequirements` (src/Realm/Data.h) só expressa marcos **absolutos** (contagens mínimas); "controlar todas
as províncias" é uma **fração do total de províncias que existem** (`Data::Get().ProvinceCount()`), que muda
por save (depende de quantos mods de território estão carregados) e não cabe no esquema de título sem alterar
o formato existente. Em vez de forçar esse encaixe (ou arriscar o `TITL` v1 já fechado), o domínio é um **marco
booleano separado, permanente, por facção**: `Realm::Domination` (src/Realm/Domination.h/.cpp), registro de
cosave próprio `DOMN` v1 (`u32 count`, depois `count × Id facção`). "Dominador(a) de Skyrim" é o **nome do
marco** na notificação e no texto do MCM — não entra na lista `Titles()` nem altera `Title::rank`.

### Fórmula: reaproveita `FactionStats::majorityProvinces`
Mesma lógica de maioria que `Government::AssignGovernor`/`Realm::ComputeStats` já usam (`Data::Governor().majority`,
padrão 50% dos territórios de uma província): domínio = `majorityProvinces.size() / Data::Get().ProvinceCount()
>= settings.json → realm.dominationThreshold` (padrão `1.0`, ou seja, maioria em TODAS as províncias existentes).
`ProvinceCount() == 0` (nenhuma província descoberta) nunca conta como dominado. O cálculo é o mesmo
`FactionStats` que `Titles::Evaluate` já recebe — nenhuma passada extra pelos territórios.

### Permanente: nunca é perdido
Como os títulos (ADR 0038, "títulos só sobem"), uma vez dominada uma facção permanece marcada mesmo que depois
perca províncias (guerra, rebelião futura da M26) — é um marco histórico ("alcançou"), não um status ao vivo.
`Realm::Domination::Of(faction)` é O(1) (um `unordered_set`); `Evaluate` só insere, nunca remove.

### Avaliação: dentro de `Realm::Reevaluate`, não um novo gancho
`Realm::Domination::Evaluate(faction, stats)` é chamado de dentro da função privada `Reevaluate` em
`src/Realm/Realm.cpp` — o mesmo lugar que já chama `Titles::Evaluate` e `Government::ValidateGovernors`, disparado
só por `OnTerritoryOwnerChanged` (troca de dono) e `OnHoursPassed` (tick estratégico). Nenhum polling, nenhuma
função nova chamada por frame. Avaliada para **qualquer facção** (não só a do jogador), como `Titles::Evaluate`
já faz — outros mods (M9/Papyrus) podem consultar `HasDominated` de uma facção de IA; só o **anúncio na tela**
(`I18n::NotifyTr`) fica restrito à facção do jogador (`a_player`), igual ao padrão de `AnnounceTitle`.

### Exposição: `Realm::HasDominated`, sem gancho no mapa tático
`Realm::HasDominated(Id a_faction)` (src/Realm/Realm.h) encaminha para `Domination::Of`. Exposto também por um
native Papyrus (`AgeOfSkyrim.HasDominated(chave_facção)`, src/Papyrus/Api.cpp) e uma linha no MCM (página
"Reino", barato: um `AddHeaderOption`/`AddTextOption` de leitura, sem nova tela) — conforme pedido, "se for
barato". **Não foi adicionado como camada do mapa tático (M11/`WarMap::AddProvider`)**: o provedor de camadas do
`WarMap` é por **território** (`Provider = std::function<json(const Territory&)>`), e domínio é um fato da
**facção**, não de um território — não há um encaixe natural sem inventar uma convenção nova no snapshot
(`docs/warmap-schema.md`) ou na view PrismaUI, o que aumentaria o risco de conflito com a M26 (que também quer
"camada nova" no mapa tático, por outro agente). Como o próprio `docs/ROADMAP.md` already diz da M27 "não é um
requisito para nenhuma outra missão", a consulta direta (`Realm::HasDominated`/API/Papyrus) é suficiente por
ora; UI do mapa fica para quando a M26 decidir o formato de camadas "globais" (não por território).
Não foi adicionado nenhum tipo de evento novo em `include/AgeOfSkyrimAPI.h` (interface pública versionada) para
não arriscar uma mudança de ABI fora do processo normal de versionamento da M9 — a consulta direta já cobre o
caso de uso descrito.

## Consequências
- `src/Realm/Domination.h/.cpp` novo; `Realm.h` ganha `HasDominated`; `Realm.cpp::Reevaluate` ganha duas linhas
  (avaliar + notificar); `Realm::Clear()` chama `Domination::Get().Clear()`.
- `settings.json → realm.dominationThreshold` (0.0–1.0, padrão 1.0).
- Texto novo `realm.domination` (en/pt-br) para o aviso único.
- Se no futuro quisermos o domínio como requisito de outra missão (ex.: M28 release, ou uma futura "corte real"),
  dá para promovê-lo a um título real de `Titles/*.json` sem quebrar o cosave: `Domination` continua existindo
  só para o caso "fração de provincias", e um título novo pode usar `territories/provinces` com os números do
  save atual como aproximação, documentado separadamente nessa hora.
