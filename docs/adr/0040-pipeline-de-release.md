# 0040. Release em tag `v*` reaproveitando o artefato do build do mesmo commit

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M23)

## Contexto
A M23 pede que o CI publique um release a cada tag `v*`. O DLL só compila no runner Windows (ADR 0006), num
build de 5–15 min, e o `build.yml` já gera o zip do mod como artefato `AgeOfSkyrim-<sha>`. O `build.yml` é
mexido por outras missões em paralelo (compilação de Papyrus, geração de ESP), então o release não deve exigir
mudanças nele. Opções avaliadas:

| Opção | Problema |
|---|---|
| Tornar o `build.yml` reutilizável (`workflow_call`) | Exige editar o `build.yml` (conflito com as outras missões) e recompila na tag |
| `workflow_run` (release dispara quando o Build termina) | O Build não roda em push de tag (`branches:` só); exigiria decidir "é release?" a partir de um run de branch, sem a tag |
| Recompilar no release | Duplica o `build.yml` (que muda com as missões) e o binário lançado não é o que foi testado |
| **Baixar o artefato do Build para o mesmo SHA** | Precisa esperar/disparar o build se ele não existir |

## Decisão
- `.github/workflows/release.yml` dispara em push de tag `v*` (e por `workflow_dispatch` com a tag, para refazer),
  roda num `ubuntu-latest` e **não compila**.
- `tools/wait_build_run.py` (só biblioteca padrão, testado com API falsa) consulta a API de Actions pelo run do
  `build.yml` com `head_sha` = commit da tag (`git rev-parse HEAD` depois do checkout, para não pegar o SHA do
  objeto de tag anotada) e com o artefato `AgeOfSkyrim-<sha>` ainda válido:
  - run com sucesso + artefato → usa;
  - run em andamento → espera (até 45 min, consulta a cada 30 s);
  - nenhum, só falhas, ou artefato expirado (retenção de 90 dias) → dispara o `build.yml` por `workflow_dispatch`
    **na tag** (o `build.yml` já aceita `workflow_dispatch`; eventos de dispatch feitos com o `GITHUB_TOKEN`
    criam runs) e espera esse run; se ele falhar, o release falha com o id do run.
- O download fica com `gh run download` (ferramenta oficial, já no runner) e a montagem com
  `tools/package_release.py` (ADR 0041), que também gera `RELEASE_NOTES.md`.
- **Versão:** a tag `vX.Y.Z[-sufixo]` precisa bater com `project(VERSION X.Y.Z)` do `CMakeLists.txt` (o DLL
  reporta essa versão ao SKSE); hífen na tag = pré-release.
- **Notas:** a seção `## [X.Y.Z]` do `CHANGELOG.md` (Keep a Changelog, em português), sem ela o release falha;
  mais um bloco fixo bilíngue de instalação/requisitos.
- Antes de empacotar, o release roda `tests/` (unittest), `check_translations.py` e `package_release.py --check`.
  Os mesmos testes rodam a cada push no workflow `Tools` (`tools.yml`, ubuntu), separado do `build.yml`.
- Release já existente para a tag → substitui o zip (`--clobber`) e as notas, em vez de falhar.
- Upload no Nexus continua manual (`docs/RELEASE.md`): a API de upload do Nexus não é pública para autores.
- Perfil de performance antes do release com `tools/perf_report.py` (`docs/PERFORMANCE.md`).

## Consequências
- O zip lançado é byte a byte o artefato do build daquele commit; nada a manter em dobro quando o `build.yml`
  ganhar Papyrus/ESP (o que estiver em `package/` entra no núcleo do FOMOD).
- Tag criada logo depois do push na `main` funciona: o release espera o build em andamento.
- Depende do nome do artefato `AgeOfSkyrim-${{ github.sha }}` no `build.yml`; se ele mudar, mudar
  `wait_build_run.py` e o passo de download junto.
- O job pode ficar até ~1 h parado esperando o build (limite `timeout-minutes: 60`), consumindo minutos de runner
  Linux (baratos), não do Windows.
