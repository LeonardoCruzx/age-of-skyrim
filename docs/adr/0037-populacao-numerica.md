# 0037. População numérica por território, crescimento logístico e cosave compacto

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M19)

## Contexto
A M19 pede população por território que cresce com comida e cai com fome, guerra e alistamento, sem atores
(ADR 0031), base da mão de obra (produção), dos recrutas (M20) e dos impostos.

## Decisão
- Um `float` por território num vetor indexado como `Config::Territories()` (lookup O(1) por `Config::IndexOf`).
  Inicial por tipo em `Population/*.json` (`initial`, + `territories` para valores por território).
- **Tick** (`Realm::OnHoursPassed`): crescimento **logístico** até `inicial × capacityFactor` com comida
  (`growthPerDay × modificador PopulationGrowth`); sem comida, queda de `starvationPerDay`. O(territórios).
- **Comida por gancho:** `Population::SetFoodProvider(fn(facção) -> bool)`, chamado uma vez por facção dona por tick e
  fora do mutex; sem gancho = comida suficiente. Territórios neutros sempre se sustentam.
- **Guerra:** troca de dono tira `captureLoss` (15%). **Alistamento:** `Draft(território, n)` para a M20, que nunca
  desce de `draftReserve × inicial`.
- **Mão de obra:** `população / inicial` limitado a `[manpowerMin, manpowerMax]`, aplicado ao `Production` de
  `Modifiers::GetFor(facção, território, ...)`.
- Cosave `POPL` v1 **compacto**: só territórios cuja população arredondada difere da inicial, `{u32 id, u16 pop}`
  (6 bytes cada; ~3 KB com 500 territórios). Tamanho logado ao salvar. A fração perdida no arredondamento é
  irrelevante para o jogo.

## Consequências
- A economia (M12) chama `OnHoursPassed` e registra o gancho de comida; impostos = população × taxa × `Tax`.
- Mudar `initial` no JSON vale para saves existentes nos territórios que ainda estão no valor padrão.
- População acima de 65535 num território é truncada no cosave (cidade padrão: 800, teto 1600).
