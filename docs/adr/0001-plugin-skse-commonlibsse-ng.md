# 0001. Um único DLL SKSE com CommonLibSSE-NG

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Técnica

## Contexto
O mod precisa de lógica pesada (estado de territórios, IA, economia) com bom desempenho e suporte a SE e AE.

## Decisão
Todo o mod é um único plugin SKSE em C++23 usando CommonLibSSE-NG (submódulo em `extern/`), com Address Library. Um DLL serve SE e AE; VR fica de fora.

## Consequências
- Lógica no DLL, rápida e sem scripts Papyrus pesados.
- Só compila no Windows com MSVC (ver ADR 0006).
- Hard deps: SKSE64 + Address Library.
