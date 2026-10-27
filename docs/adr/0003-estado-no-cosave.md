# 0003. Estado do mod no cosave do SKSE, não em Papyrus

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Técnica

## Contexto
Estado guardado em scripts Papyrus pesa no save, é lento e frágil.

## Decisão
Todo o estado vive no DLL e é salvo no cosave (`.skse`), em registros com tag de 4 letras + versão (`TERR`, `PLAC`, `TEST`...). Um único laço em `Main.cpp` lê os registros e despacha cada um ao seu módulo.

## Consequências
- Saves leves e rápidos; nada a limpar no Papyrus ao desinstalar.
- Toda mudança de formato sobe a versão do registro, com migração.
