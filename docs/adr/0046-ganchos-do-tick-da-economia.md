# 0046. Ganchos do tick da economia: listeners por hora e multiplicador por território

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica

## Contexto
A [ADR 0026](0026-tick-por-hora-orientado-a-evento.md) pede que obras (M15), guarnição (M14) e IA (M16) pendurem no
mesmo tick por hora em vez de relógios próprios, mas a `Economy` não tinha como chamar outros módulos depois do tick
nem como variar a produção por território.

## Decisão
- `Economy::AddTickListener(chave, fn(horas))`: chamado na thread principal **depois** de cada tick e de
  `DebugForceHours`, **fora** do lock da economia (o listener pode consultar `DeficitHours`, `Balance` etc.). Chave
  repetida substitui. Horas = as mesmas processadas pela produção (já limitadas por `maxCatchUpHours`).
- `Economy::SetTerritoryMultiplier(fn(território) -> double)`: multiplica a produção de cada território dentro do
  tick (lock da economia travado: o provider não pode chamar a Economy). Usado pela melhoria de renda (M15).
- Ordem de locks: Economy → (Garrison | Upgrades | State | Factions | Config); Garrison → Upgrades. Nenhum módulo
  chama a Economy segurando o próprio lock.

## Consequências
- M16 (IA) e M19 (população) podem pendurar no tick com uma linha.
- Um único multiplicador por território: outros efeitos (doutrina M18, população M19) devem compor dentro do mesmo
  provider ou trocar o gancho por uma lista quando precisarem.
