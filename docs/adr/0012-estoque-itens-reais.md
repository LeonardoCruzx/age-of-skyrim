# 0012. Estoque da sede com itens reais contados por categoria

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Decisão do usuário

## Contexto
A sede precisa de um estoque de comida, ouro, metais etc.

## Decisão
Um baú da sede guarda itens de verdade. O DLL classifica por keywords vanilla (`VendorItemFood`, `VendorItemOreIngot`, ouro...) conforme `Resources.json` e soma por categoria. A recontagem só acontece ao fechar o baú.

## Consequências
- Itens de outros mods entram automaticamente se usam as keywords padrão.
- Custo zero contínuo.
