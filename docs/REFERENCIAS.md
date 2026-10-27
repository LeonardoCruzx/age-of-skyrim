# Referências externas

Fontes consultadas no design do mod. **Nenhuma delas é copiada:** código, scripts, assets e textos de outros
projetos não entram no repositório; usamos só ideias, requisitos e lições aprendidas, escritas com as nossas palavras
([ADR 0030](adr/0030-referencias-externas-dogfish.md)).

| Fonte | O que é | Como usamos |
|---|---|---|
| [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG) | Biblioteca de engenharia reversa do Skyrim para plugins SKSE (submódulo em `extern/`) | Base do DLL; headers são a referência de toda API `RE::` |
| [Conquest of Skyrim](https://www.nexusmods.com/skyrimspecialedition/mods/65628) (galvatron304) | Mod de grande estratégia em Papyrus/ESP (v0.3.9, 2023), principal referência do gênero | Referência de **funcionalidades** que jogadores esperam; nada do conteúdo é usado |
| [Dogfish](https://github.com/bearingme/Dogfish) (bearingme) | Pesquisa e recuperação de especificação do Conquest of Skyrim: inventário de funcionalidades e de problemas conhecidos | Checklist de funcionalidades e de falhas a evitar → [comparativo](COMPARATIVO-CONQUEST-OF-SKYRIM.md) |

Cuidados com o Dogfish: projeto novo (outubro de 2026), sem licença declarada, só documentação; vários itens estão
marcados por eles como não verificados (`UNV`). Tratamos como **hipóteses de requisito**, não como fatos.
