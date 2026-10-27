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
| CTD ao carregar cerco com 40+ atores e Papyrus congestionado (`ISSUE-CTD-01`, `-04`) | Lógica no DLL, sem loops Papyrus; **teto global de atores** criados e spawn em lotes por frame | M14, M16 |
| Crash de pathfinding com navmesh alterado (`ISSUE-CTD-02`) | Nunca editar navmesh/células vanilla; NPCs só em marcadores existentes | M14, M16 |
| Soldados sem FaceGen (`ISSUE-CTD-03`) | Usar NPCs/leveled lists vanilla com FaceGen pronto, nada de NPC gerado sem malha | M14 |
| "Tropas peladas": outfit não aplicado (`ISSUE-GAME-01`) | Aplicar outfit no DLL ao criar o ator e conferir no carregamento da célula | M14 |
| Recompensa (bounty) da facção antiga continua (`ISSUE-GAME-02`) | Ao trocar dono de cidade, tratar a facção de crime da província (opcional, configurável) | M16/M17 |
| Conflito com quests da Guerra Civil vanilla (`ISSUE-QST-03/04`) | Lista de exclusão e detecção de quests da Guerra Civil ativas; não tocar aliases vanilla | M5, M18 |
| Contagem de capitais dessincronizada e captura que não dispara (`ISSUE-QST-01/02`) | Estado derivado de uma única fonte (State) + evento de local limpo, sem gatilhos de proximidade | M5 |
| Pacotes de IA presos (`ISSUE-GAME-03`) | Não reaproveitar NPCs vanilla com pacotes/aliases; criar os nossos | M14 |

## 3. Funcionalidades do CoS que NÃO estão no roadmap (candidatas)

Decisão do usuário se entram como missões novas:

- **Recrutamento e exército:** oficiais, tropas, divisões, equipamento padrão, treino (`FEAT-MIL-01..05`).
- **Batalhas em tempo real:** formações, desfiles de vitória (`FEAT-BAT-01..03`).
- **Cercos de capitais:** início do cerco, armas de cerco, invasão, rendição (`FEAT-SIEG-01..04`).
- **Governo:** doutrina (feudal/militar/república), governadores e troca de jarl, mordomo/comandante/ferreiro
  (`FEAT-FACT-04`, `FEAT-SETT-01/02`, `FEAT-GOV-*`, `FEAT-JARL-*`).
- **População:** contagem, trabalho, alistamento (`FEAT-POP-01..03`).
- **Lei:** recompensas, prisioneiros, resgates, execuções, prisões (`FEAT-LAW-*`, `FEAT-DIPL-03`).
- **Estabilidade:** ordem pública, rebeliões (`FEAT-STAB-01/02`).
- **Progressão e vitória:** títulos, corte, conquistar as 9 províncias, Moot e coroação
  (`FEAT-PROG-*`, `FEAT-VICT-*`).
- **Pesquisa militar** (`FEAT-TECH-01`).
- **Diálogo imersivo** com NPCs da facção (`FEAT-UI-01`), o que exige ESP com quests e diálogo, e voz.
