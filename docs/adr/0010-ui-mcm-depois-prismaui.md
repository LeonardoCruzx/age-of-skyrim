# 0010. Interface: MCM primeiro, PrismaUI depois, sobre a mesma API

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Decisão do usuário

## Contexto
O mod precisa de UI para facção, territórios e gestão.

## Decisão
Primeiro um MCM (SkyUI) fino em Papyrus; depois PrismaUI (HTML/JS) para o mapa tático. Ambos chamam a mesma camada de API nativa do DLL; nenhuma lógica na UI.

## Consequências
- Trocar ou somar UI não mexe na lógica.
- SkyUI e PrismaUI são dependências opcionais.
