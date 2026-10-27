# 0006. Build pelo GitHub Actions (windows-2022 + vcpkg recente)

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Técnica

## Contexto
O ambiente de desenvolvimento é um container Linux; o DLL exige MSVC no Windows.

## Decisão
O CI compila a cada push num runner `windows-2022` (o gerador do preset é VS 2022). O vcpkg é clonado na tag `2026.07.29` (ferramenta + scripts atuais), enquanto o `builtin-baseline` do `vcpkg.json` continua fixando as versões das bibliotecas. Cache binário em disco via `actions/cache`. O zip do mod sai como artefato. Em falha, o fim dos logs vira anotação pública.

## Consequências
- Valido o código sem Windows; o usuário baixa o zip pronto.
- O checkout antigo do baseline não serve mais (mirrors do MSYS2 com 404).
