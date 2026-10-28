# 0026. Tick da economia por hora de jogo, disparado por eventos baratos

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Técnica

## Contexto
A economia (M12), as rotas (M13) e depois as obras (M15) precisam avançar conforme o tempo do jogo passa, no
máximo 1×/hora de jogo e sem nada por frame ([ADR 0005](0005-principios-performance-compatibilidade.md)).
A CommonLibSSE-NG não tem um evento "passou uma hora": `RE::Calendar` só expõe o relógio
(`GetHoursPassed()` = `GameDaysPassed × 24`), e um `RegisterForUpdateGameTime` exigiria um script Papyrus.

## Decisão
- **Relógio:** `RE::Calendar::GetSingleton()->GetHoursPassed()`; o último tick (horas, `double`) fica no cosave.
- **Gatilhos** (sinks registrados uma vez em `Economy::Load`), todos eventos que o jogo já dispara:
  `MenuOpenCloseEvent` (só ao **fechar** um menu: inventário, mapa/viagem rápida, sono/espera, diálogos),
  `TESCellFullyLoadedEvent` (andar pelo mundo, entrar/sair de interiores), `TESSleepStopEvent` e
  `TESWaitStopEvent`. Cada evento custa uma leitura do relógio e uma comparação; só quando passou ≥ 1 hora
  inteira é agendado `Economy::Tick` na thread principal (`AddTask`), com uma flag atômica para não enfileirar
  dois ticks.
- **Catch-up em lote:** o tick processa `floor(agora − último)` horas de uma vez. Produção, renda e consumo são
  lineares nas horas, então N horas custam o mesmo que 1 (O(territórios × regras + rotas × territórios)).
  Acima de `maxCatchUpHours` (Economy.json, padrão 168) o excesso é descartado (log `warn`); a fração da hora
  corrente é preservada.
- **Início:** sem registro `ECON` (jogo novo, save antigo) ou relógio voltando para trás, o primeiro evento só
  marca a hora atual — não há produção retroativa. `kNewGame` zera o estado (o revert do SKSE não roda aí).
- Tempo medido no log (`[tempo] Economy::Tick`); meta < 5 ms.

## Consequências
- Ficar parado ao ar livre sem abrir menus nem trocar de célula adia o tick, mas nada se perde: o próximo
  evento processa as horas acumuladas.
- Outros sistemas por hora (M15 obras, M16 IA) devem pendurar no mesmo `Economy::Tick` em vez de criar
  relógios próprios.
- Alternativas descartadas: hook no update do jogo (por frame), timer em thread própria (tempo real ≠ tempo de
  jogo e acesso fora da thread principal), script Papyrus com `RegisterForSingleUpdateGameTime` (exige ESP/script).
