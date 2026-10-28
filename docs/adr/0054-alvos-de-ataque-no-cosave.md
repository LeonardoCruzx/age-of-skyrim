# 0054. Alvos de ataque marcados no mapa ficam no cosave (`WMAP` v1)

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M11)

## Contexto
O mapa permite marcar alvos de ataque. A IA/ataques (M16) ainda não existe, mas vai consumir essas marcações; o
jogador espera que elas sobrevivam a salvar/carregar.

## Decisão
- Guardar no cosave, registro **`WMAP` v1**: `u32 contagem` + `u32 território` × N, na ordem de marcação.
  Território que não existe mais ao carregar (mod removido) é descartado com log. Limpo no revert / jogo novo.
  Gravado depois de `CLAM` (claims podem ser alvos).
- Limite `settings.json → warMap.maxTargets` (padrão 10); não é possível marcar território da própria facção.
- API: `WarMap::Targets()` / `IsTarget()` (thread-safe) para a M16; `SetTarget(id, on)` para as UIs.
- Descartado: só em memória — perderia o planejamento ao carregar e obrigaria a M16 a criar o próprio registro.

## Consequências
- Poucos bytes por save. A M16 decide o que fazer com os alvos (prioridade de ataque, avisos).
