# 0039. Módulo `src/Realm`: modificadores agregados, ganchos de integração e relógio próprio

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M18/M19/M22)

## Contexto
M18, M19 e M22 foram feitas antes da economia (M12, que tem o tick por hora), da sede (M6), do recrutamento (M20)
e da API (M9), em paralelo com elas. Era preciso um módulo autocontido com pontos de integração estáveis.

## Decisão
- **`Realm::Modifiers`**: `Get(facção, Modifier::X)` = `1 + Σ bônus` (doutrina + cargos + título), mínimo 0,1;
  `GetFor(facção, território, X)` soma o governador da província e, para `Production`, multiplica pela mão de obra.
  Sem cache: são poucas somas de arrays de 8 floats por consulta.
- **`Realm::OnHoursPassed(horas)`**: a economia (M12) chama no tick dela; o reino **não cria tick próprio**.
  Faz população + títulos em O(territórios), medido com `ScopedTimer` (meta < 2 ms).
- **`Realm::OnTerritoryOwnerChanged(território, antigo, novo)`**: chamado por `SetTerritoryOwner` só quando
  `State::SetOwner` relata mudança (3 linhas, bem delimitadas).
- **Ganchos registráveis** (`std::function`, chamados fora do mutex): comida (`Population::SetFoodProvider`) e
  pagamento (`Government::SetPaymentHandler`). Padrões: comida suficiente, troca grátis.
- **Relógio do reino**: horas acumuladas por `OnHoursPassed`, salvas no `GOVT`. Esperas (doutrina) usam esse relógio,
  não o calendário do jogo — determinístico, e a simulação por hotkey avança a espera junto com a população.
  Quando a M12 chamar `OnHoursPassed` com as horas reais do jogo, os dois relógios andam juntos.
- **Um mutex recursivo** para todo o estado do reino (`Realm::Mutex()`); anúncios vão para a thread principal por
  `AddTask`. Zero atores, zero Papyrus, nada por frame.
- **Debug** (`settings.json → debug`): F7 resumo, F6 simula `realmDebugHours`, F3 próxima doutrina, F4 entrega os
  `realmConquerCount` territórios mais próximos à facção do jogador e preenche cargos/governadores. Sink de input
  próprio no módulo (não mexe no de `Events.cpp`).

## Consequências
- M12: chamar `Realm::OnHoursPassed`, registrar os dois ganchos e ler `Modifiers` (Production/Tax/Upkeep/Trade).
- M20: `Population::Draft`, `Modifiers` (RecruitCost/RecruitSpeed) e `Titles::Unlock("armyCap")`.
- M9: expor essas mesmas funções na API/Papyrus; M10/M11: telas sobre `Government`/`Titles`/`Population`.
- Se a M12 decidir usar o calendário do jogo para esperas, basta trocar `Government::Clock()`.

## Atualização (merge com a M12)
- `Economy::Tick()` devolve as horas processadas; a tarefa do tick chama `Realm::OnHoursPassed` **fora** do lock da
  economia (o provedor de comida consulta a economia, então chamar dentro travaria). O F6 da economia também avança o reino.
- Produção por território na economia multiplicada por `Realm::Modifiers::GetFor(dono, território, Production)`.
- Provedor de comida: facção sem déficit de manutenção (`!Economy::InDeficit`).
- Pagamento da troca de doutrina continua grátis: o custo é por categoria do estoque e retirar itens do baú por
  categoria fica para quando houver a mesma necessidade nas melhorias (M15).
- Hotkeys do reino movidas para NumPad1–4 (F3/F4/F6/F7 já eram de claim, conquista, economia e rota).
