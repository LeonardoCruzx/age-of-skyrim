# 0002. IDs estáveis por hash FNV-1a das chaves de texto

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Técnica

## Contexto
Territórios e estilos são definidos em JSON; o cosave precisa de identificadores que não quebrem quando o JSON é reordenado ou editado.

## Decisão
Cada chave de texto (`"test_tower"`, `"imperial"`) vira um `Id` = FNV-1a 32 bits, sem diferenciar maiúsculas. `0` é reservado para neutro. O cosave guarda só esses hashes.

## Consequências
- Reordenar/renomear o campo `name` não corrompe saves; trocar a chave `id` sim.
- Colisões são detectadas e logadas na carga.
