# 0031. Fase 4: governo e doutrina, população, exército e recrutamento, batalhas em tempo real, títulos

- Status: Aceita
- Data: 2026-10-27
- Tipo: Decisão do usuário

## Contexto
O comparativo com o Conquest of Skyrim (ADR 0030, `docs/COMPARATIVO-CONQUEST-OF-SKYRIM.md`) listou
funcionalidades do gênero que não estavam no roadmap. O usuário escolheu adicionar exército, recrutamento,
batalhas em tempo real, governo, doutrina, população e títulos.

## Decisão
Nova **Fase 4 — Reino e exército**, depois da diplomacia e antes do release:
M18 governo e doutrina · M19 população · M20 exército e recrutamento · M21 batalhas em tempo real ·
M22 títulos e progressão. O release (compatibilidade + FOMOD) passa de M18 para **M23**. M16 (IA) e M17
(diplomacia) mantêm os números, porque já são citadas no código e nos ADRs; elas ganham exércitos e batalhas
na Fase 4.

Regras de desempenho e compatibilidade, que vêm das falhas conhecidas do Conquest of Skyrim:
- População, exércitos e doutrinas são **números simulados no DLL** (tick por hora da M12), sem atores e sem Papyrus.
- Exércitos são abstratos e só viram atores perto do jogador (M21), com **teto global de atores**, criação em
  lotes por frame e reforços em ondas; longe do jogador a resolução é numérica (a mesma da M16).
- Soldados e cargos usam bases vanilla com FaceGen pronto, outfit aplicado pelo DLL e pacotes próprios; nunca
  NPCs vanilla com aliases. Nada de editar navmesh ou células.
- Tudo data-driven (`Doctrines.json`, `Troops.json`, `Titles.json`) e extensível por outros mods; textos en/pt-br.

## Consequências
- O roadmap vai de 18 para 23 missões, e as Fases 3 e 4 dependem da economia (M12) e da guarnição (M14).
- Batalhas em tempo real são a parte de maior risco para o desempenho: teto de atores configurável, tempo por
  frame medido no log e opção de desligar (só resolução numérica).
- Cercos, troca de jarl, lei, estabilidade e vitória (Moot) continuam candidatos (comparativo, seção 4).
