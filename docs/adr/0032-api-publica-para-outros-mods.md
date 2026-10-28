# 0032. API pública para outros mods: interface C++ versionada, eventos e natives Papyrus

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M9)

## Contexto
Mods de guerra civil, patrulha, economia e o próprio MCM/PrismaUI (ADR 0010) precisam consultar e reagir ao
estado do Age of Skyrim (facções, donos de território, estoque) sem depender de detalhes internos. Sede (M6),
economia (M12), rotas (M13), governo (M18), população (M19) e títulos (M22) ainda não existem, mas a API
não pode quebrar quando chegarem.

## Decisão
- **Header público** `include/AgeOfSkyrimAPI.h` (copiável, sem dependência da CommonLib exceto o auxiliar
  `RequestInterface()`): classe abstrata `IAgeOfSkyrim` com `kInterfaceVersion = 1`.
  - **ABI estável:** só tipos POD atravessam o limite do DLL (ids `uint32`, texto UTF-8 copiado para buffer do
    chamador, structs com campo `size` no início). A vtable só cresce no fim; nada é removido/reordenado.
  - Ids = mesmo hash FNV-1a das chaves (ADR 0002), exposto como `MakeId` no header.
- **Como obter:** mensagem SKSE `kMessageRequestInterface` ('AOSI') enviada a `"AgeOfSkyrim"` em `kPostLoad`
  ou depois; o mod registra `RegisterListener(nullptr, ...)` (qualquer remetente) e preenche
  `InterfaceRequest::result` na hora (Dispatch é síncrono). Alternativa: export `AoS_GetInterface(version)`.
  Versão pedida maior que a disponível → `nullptr`.
- **Eventos** (`src/Api/Events`): registro central `Api::Events::Emit(Event)`, chamável de qualquer thread; o
  evento vai para a fila da thread principal (`AddTask`) e lá é entregue aos callbacks C++ (`AddListener`) e,
  se `settings.json → api.papyrusEvents`, a um **ModEvent** `AoS_<Nome>` (`SKSE::ModCallbackEvent`:
  `strArg` = chave do território/facção, `numArg` = valor, `sender` = form ligado). Sem ouvinte e sem Papyrus,
  `Emit` sai na hora (custo zero).
  - Com emissor hoje: `StateReady` (após aplicar o estado ao mundo), `TerritoryChanged` (em `SetTerritoryOwner`,
    só quando o dono muda), `FactionCreated`, `FactionChanged`, `ClaimCreated`, `StockChanged` (recontagem de
    container observado).
  - Reservados, emitidos a partir da missão indicada: `SeatFounded`/`SeatMoved` (M6), `TreasuryChanged` (M12),
    `TradeRouteChanged` (M13), `GovernmentChanged` (M18), `PopulationChanged` (M19), `TitleGranted` (M22).
  - O Papyrus recebe só a chave do território; o dono anterior fica em memória (`GetTerritoryPreviousOwner`,
    não vai para o cosave).
- **Ganchos para sistemas futuros** (`src/Api/ModApi.h`): `SetSeatProvider`, `SetTreasuryProvider`,
  `SetTradeRouteProvider`, `SetGovernmentProvider` (`std::function`, registrados em `kDataLoaded` pelo módulo
  dono). Sem provedor, a API responde "não disponível" (`false`/0/neutro). A interface C++ e os natives já
  têm as funções (`GetSeat`, `GetTreasury`, `GetTradeRouteCount`, `GetGovernment`).
- **Natives Papyrus** (`src/Papyrus/Api`, script `AgeOfSkyrim.psc`, `Hidden`, funções `Global Native`):
  facção/território por **chave de texto** ("" = neutro), arrays limitados a 128 (lista de territórios
  paginada e ordenada por nome). Consultas sob lock rodam na thread do Papyrus (`callableFromTasklets`);
  inventário, mundo e alterações rodam na thread principal (padrão do VM).
- **Troca de dono vinda de fora** (C++ ou Papyrus) passa por `SetTerritoryOwner` e pode ser desligada em
  `settings.json → api.allowOwnerChanges`.

## Consequências
- MCM (M10) e PrismaUI (M11) usam a mesma camada; outros mods integram sem patch.
- Novos módulos só chamam `Api::Events::Emit` com o tipo deles e registram um provedor; a interface não muda.
- `GetStockTotals` (C++) é da thread principal: o `Stock` não tem lock (documentado no header).
- Nenhum registro de cosave novo.
