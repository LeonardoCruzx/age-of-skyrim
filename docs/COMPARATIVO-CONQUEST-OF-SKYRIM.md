# Comparativo: Conquest of Skyrim (via Dogfish) × Age of Skyrim

Base: inventário de funcionalidades e de problemas do [Dogfish](https://github.com/bearingme/Dogfish)
(`research/findings/`, Conquest of Skyrim v0.3.9). IDs `FEAT-*`/`ISSUE-*` são os deles, para rastrear.
Ver [REFERENCIAS.md](REFERENCIAS.md) e [ADR 0030](adr/0030-referencias-externas-dogfish.md).

## 1. Funcionalidades já cobertas pelo roadmap

| Conquest of Skyrim | Age of Skyrim |
|---|---|
| Declarar facção, nome próprio, estandartes (`FEAT-FACT-01..03`) | M3 facções do jogador + M2 bandeiras por dono |
| Tomar acampamentos e fortes (`FEAT-TERR-01/02`) | M5 conquista por local limpo + M8 Free Claim |
| Hierarquia de territórios (`FEAT-TERR-03`) | M4 tipos de local (camp/fort/tower/village/city) + província |
| Guarnições (`FEAT-SETT-03`) | M14 |
| Tesouro, recursos, manutenção (`FEAT-ECON-01/03/04`) | M7 estoque + M12 economia |
| Construções e melhorias (`FEAT-INFR-01/02`, `FEAT-TECH-02`) | M15 melhorias |
| Batalhas fora da tela (`FEAT-BAT-04`) | M16 IA e contra-ataques |
| Guerra, trégua e paz (`FEAT-DIPL-01/02`) | M17 diplomacia |
| MCM (`FEAT-UI-02`) | M10 (+ mapa tático M11, que o CoS não tem) |

## 2. Lições dos problemas conhecidos (aplicar nas missões)

| Problema no CoS | Regra para nós | Missão |
|---|---|---|
| CTD ao carregar cerco com 40+ atores e Papyrus congestionado (`ISSUE-CTD-01`, `-04`) | Lógica no DLL, sem loops Papyrus; **teto global de atores** criados e spawn em lotes por frame | M14, M16, M20, M21 |
| Crash de pathfinding com navmesh alterado (`ISSUE-CTD-02`) | Nunca editar navmesh/células vanilla; NPCs só em marcadores existentes | M14, M16, M21 |
| Soldados sem FaceGen (`ISSUE-CTD-03`) | Usar NPCs/leveled lists vanilla com FaceGen pronto, nada de NPC gerado sem malha | M14, M20 |
| "Tropas peladas": outfit não aplicado (`ISSUE-GAME-01`) | Aplicar outfit no DLL ao criar o ator e conferir no carregamento da célula | M14, M20, M21 |
| Recompensa (bounty) da facção antiga continua (`ISSUE-GAME-02`) | Ao trocar dono de cidade, tratar a facção de crime da província (opcional, configurável) | M16/M17 |
| Conflito com quests da Guerra Civil vanilla (`ISSUE-QST-03/04`) | Lista de exclusão e detecção de quests da Guerra Civil ativas; não tocar aliases vanilla | M5, M28 |
| Contagem de capitais dessincronizada e captura que não dispara (`ISSUE-QST-01/02`) | Estado derivado de uma única fonte (State) + evento de local limpo, sem gatilhos de proximidade | M5 |
| Pacotes de IA presos (`ISSUE-GAME-03`) | Não reaproveitar NPCs vanilla com pacotes/aliases; criar os nossos | M14, M18, M20 |

## 3. Adotadas na Fase 4 ([ADR 0031](adr/0031-exercito-governo-populacao-titulos.md))

| Conquest of Skyrim | Age of Skyrim |
|---|---|
| Doutrina, cargos (mordomo/comandante/ferreiro), governadores (`FEAT-FACT-04`, `FEAT-GOV-*`, `FEAT-SETT-02`) | M18 governo e doutrina |
| População, trabalho, alistamento (`FEAT-POP-01..03`) | M19 população |
| Oficiais, tropas, divisões, equipamento, treino (`FEAT-MIL-01..05`) | M20 exército e recrutamento |
| Batalhas em tempo real, formações (`FEAT-BAT-01/02`) | M21 batalhas em tempo real |
| Títulos (`FEAT-PROG-01`) | M22 títulos e progressão |

## 4. Adotadas na Fase 5 ([ADR 0055](adr/0055-dominio-suave-lei-ordem-jarls.md))

| Conquest of Skyrim | Age of Skyrim |
|---|---|
| Troca de jarl, lealdade (`FEAT-JARL-*`, `FEAT-SETT-01`) | M24 troca de jarl / governo local |
| Lei: recompensas, prisioneiros, resgates, execuções, prisões (`FEAT-LAW-*`, `FEAT-DIPL-03`) | M25 lei e prisioneiros |
| Estabilidade: ordem pública, rebeliões (`FEAT-STAB-01/02`) | M26 ordem pública e rebeliões |
| Vitória: conquistar as províncias (`FEAT-VICT-01`) | M27 Domínio de Skyrim — **vitória suave**: título + aviso, sem travar nenhum sistema (diferente do Moot/coroação do CoS, que encerra o jogo) |

Recusados (ADR 0055): diálogo com voz (`FEAT-UI-01`, só texto en/pt-br) e cerco de capitais (`FEAT-SIEG-01..04`).

## 5. Funcionalidades do CoS ainda fora do roadmap (candidatas)

- **Cercos de capitais:** início do cerco, armas de cerco, invasão, rendição (`FEAT-SIEG-01..04`).
- **Desfiles de vitória** (`FEAT-BAT-03`), **corte e guarda real** (`FEAT-PROG-02`).
- **Moot e coroação** como final fechado (`FEAT-VICT-02/03`) — recusado; ver M27.
- **Pesquisa militar** (`FEAT-TECH-01`).
- **Diálogo com voz** (`FEAT-UI-01`) — recusado (ADR 0055); diálogo em texto pode entrar sem essa restrição.
