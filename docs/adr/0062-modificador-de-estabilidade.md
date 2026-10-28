# 0062. `Realm::Modifier::Stability`: fórmula de estabilidade por território

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M26)

## Contexto
A M26 pede um modificador novo de estabilidade, com fontes em doutrina, governador, guarnição vs. população e
déficit de comida/ouro. `Realm::Modifiers` (M18, `src/Realm/Modifiers.h`) já agrega doutrina + cargos + título +
governador como um multiplicador por território (`Modifiers::GetFor`); a pergunta era se dava para reaproveitar
sem quebrar o cosave do Government/Titles.

## Decisão
- **`Modifier::Stability` aditivo, no fim do enum** (depois de `Upkeep`). Confirmado seguro: `ModifierSet`
  (`std::array<float, kModifierCount>`) **nunca é serializado** no cosave — `Government`/`Titles` só persistem
  *ids* de doutrina/cargo/título (`GOVT`/`TTLS`) e recalculam o `ModifierSet` inteiro em runtime, a cada consulta,
  a partir do JSON atual (`Government::AddModifiers`, `Titles::AddModifiers`, ambos com `for (i < kModifierCount)`
  genérico). Crescer o enum não muda nenhum formato de save antigo.
- **Fórmula** (`Stability::ComputeScore`, `src/Sim/Stability.cpp`), por território, dono != neutro:
  ```
  score = stability.baseline × Modifiers::GetFor(dono, território, Stability)   // doutrina + governador
  if guarnição (Garrison::Strength) < população × stability.popToStrength:
      score -= (1 - guarnição/necessária) × stability.garrisonPenalty
  if Economy::InDeficit(dono):               // comida OU ouro, qualquer déficit corrente
      score -= stability.deficitPenalty
  score -= abalo de execuções recentes (Law::ReportUnrest, decai stability.unrestDecayPerHour por hora)
  score = clamp(score, 0, 100)
  ```
  `Garrison::Strength` (não `Size`) foi escolhido porque já inclui o bônus de melhorias (muralhas) e o
  multiplicador de obra em andamento — "guarnição presente" de fato, não só o número bruto de soldados.
- **Dados de exemplo:** `Doctrines/default.json` ganhou `"stability"` em todas as doutrinas (feudal +10%,
  militar -5%, república +5%, mercantil -5%) e no governador (+10%), só para demonstrar o campo — qualquer mod
  de dados pode ajustar ou ignorar.
- **Tick:** pendurado em `Economy::AddTickListener("stability", ...)`, o mesmo gancho de Garrison/Upgrades/Ai/
  Diplomacy (ADR 0026/0046), nunca por frame.

## Consequências
- Custo O(territórios donos) por tick de economia (no máximo 1×/hora de jogo), igual ao resto do reino.
- Qualquer mod de dados pode adicionar `"stability"` em `Doctrines/*.json` próprios sem tocar neste código.
