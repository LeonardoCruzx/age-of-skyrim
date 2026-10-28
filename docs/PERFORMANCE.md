# Perfil de performance

Metas do projeto ([ADR 0005](adr/0005-principios-performance-compatibilidade.md), [Roadmap](ROADMAP.md#verificação)):

| Tipo | Exemplos | Meta |
|---|---|---|
| **event** — reação a um evento do jogo ou hotkey | `Stock::CountByCategory` (fechar baú), `Claims::TryClaimHere` (F3), conquista de local | **< 1 ms** |
| **tick** — passo estratégico (no máximo 1×/hora de jogo) | economia, IA, rotas (nomes com `Tick`/`Hourly`) | **< 5 ms** |
| **load** — uma vez por carregamento | `Discovery::Scan`, `Config::*`, `Stock::Load`, `BannerController::ApplyAll` | informativo |
| **debug** | `SelfTest::Run` (F11) | informativo |

## De onde vêm os números

Toda operação relevante do DLL é medida com `ScopedTimer` (`src/Core/Timer.h`), que ao sair do escopo escreve no
log uma linha assim:

```
[18:04:02.880] [info] [tempo] Stock::CountByCategory: 0.210 ms
```

- `[tempo]` marca a linha; `Stock::CountByCategory` é a operação (`Módulo::Função`); o número é o tempo de parede
  (`std::chrono::steady_clock`) em milissegundos, com 3 casas.
- O tempo inclui só o trabalho do DLL na thread em que roda (normalmente a principal). Trabalho agendado com
  `SKSE::GetTaskInterface()->AddTask` aparece separado, na operação da tarefa.
- O log fica em `Documents\My Games\Skyrim Special Edition\SKSE\AgeOfSkyrim.log` e é recriado a cada sessão.

Operação nova = `ScopedTimer t{ "Módulo::Função" };` no começo dela. Se for um tick, ponha `Tick` ou `Hourly` no
nome para o relatório aplicar a meta de 5 ms (ou registre a categoria em `KNOWN` no `tools/perf_report.py`).

## Relatório

```
python3 tools/perf_report.py "AgeOfSkyrim.log"
```

Mostra, por operação: tipo, número de amostras, mínimo, média, p95, máximo, meta e status (`ok`, `ALERTA (n/N
acima)` ou `info`). Opções:

- `--strict` — código de saída 1 se alguma meta estourar (para usar em script).
- `--target Operação=ms` — meta específica (repetível), ex.: `--target Discovery::Scan=50`.
- `--github` — alertas como anotações `::warning` do GitHub Actions.

Exemplo de saída com o log de teste (`tests/data/AgeOfSkyrim_sample.log`):

```
operação                           tipo       n       mín     média       p95       máx   meta  status
Stock::CountByCategory             event      3     0.210     0.653     1.420     1.420      1  ALERTA (1/3 acima)
Economy::HourlyTick                tick       2     3.200     4.975     6.750     6.750      5  ALERTA (1/2 acima)
Claims::TryClaimHere               event      1     0.640     0.640     0.640     0.640      1  ok
Discovery::Scan                    load       1    14.812    14.812    14.812    14.812      -  info
```

## Perfil num save grande (M28)

Antes de cada release, com o máximo de conteúdo ativo (todos os territórios descobertos, guarnições, exércitos,
IA, rotas e batalhas, conforme as missões existirem):

1. Carregue o save, jogue ~2 horas de jogo (espere/viaje rápido para disparar os ticks) e use as funções que
   reagem a eventos (baú da sede, conquista, mesa de guerra).
2. Saia do jogo e rode `python3 tools/perf_report.py AgeOfSkyrim.log`.
3. Todo `ALERTA` vira issue antes do release; o resumo (operações e máximos) vai para as notas do CHANGELOG.

Picos isolados no primeiro evento depois do load (cache frio, ex.: classificação de itens do estoque) são
esperados; o que importa é a média e o p95 ficarem dentro da meta.
