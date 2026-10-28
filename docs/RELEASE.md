# Como lançar uma versão

Decisões: [ADR 0040](adr/0040-pipeline-de-release.md) (pipeline), [ADR 0041](adr/0041-fomod-layout.md) (instalador),
[ADR 0042](adr/0042-patches-de-compatibilidade.md) (patches).

## 1. Preparar (na `main`, CI verde)

1. **Versão:** suba `project(AgeOfSkyrim VERSION X.Y.Z ...)` no `CMakeLists.txt` e `version-string` no `vcpkg.json`.
   O release falha se a tag não bater com o `CMakeLists.txt` (sufixos como `-beta.1` são ignorados na comparação).
2. **Changelog:** em `CHANGELOG.md`, renomeie `## [Não lançado]` para `## [X.Y.Z] - AAAA-MM-DD` e crie uma
   `## [Não lançado]` vazia acima. A seção da versão vira o texto do GitHub Release.
3. **settings.json de jogador:** `"language": "auto"` e `"debug": { "enabled": false }`. O pacote só avisa
   (`::warning`), não bloqueia.
4. **Perfil de performance:** rode o roteiro de [PERFORMANCE.md](PERFORMANCE.md#perfil-num-save-grande-m23) e
   resolva os `ALERTA`.
5. **Conferência local** (opcional): `python3 -m unittest discover -s tests` e `python3 tools/package_release.py --check`.
6. Commit + push na `main` e espere o **Build** ficar verde (é o artefato desse build que vai para o release).

## 2. Criar a tag

```bash
git tag -a vX.Y.Z -m "Age of Skyrim X.Y.Z"
git push origin vX.Y.Z
```

Tag com hífen (`v0.2.0-beta.1`) sai como **pré-release**.

## 3. O que o CI faz (`.github/workflows/release.yml`)

1. Checkout da tag; roda `tests/` (python) e confere traduções, `fomod/` e `patches/`.
2. `tools/wait_build_run.py` acha o run do **Build** do mesmo commit com o artefato `AgeOfSkyrim-<sha>`:
   - build ainda rodando → espera (até 45 min);
   - nenhum build com sucesso ou artefato expirado (90 dias) → dispara o `build.yml` na tag (workflow_dispatch) e espera.
3. `gh run download` baixa o artefato; `tools/package_release.py` monta `AgeOfSkyrim-X.Y.Z.zip` no layout FOMOD
   e `RELEASE_NOTES.md` (seção do changelog + requisitos).
4. Cria o GitHub Release `vX.Y.Z` com o zip (se já existir, substitui o zip e as notas) e guarda `dist/` como
   artefato do run.

Para refazer um release sem nova tag: *Actions → Release → Run workflow* com `tag = vX.Y.Z`.

## 4. Nexus Mods (manual)

1. Baixe `AgeOfSkyrim-X.Y.Z.zip` do GitHub Release e **teste a instalação** no MO2 (o FOMOD abre, o patch é
   opcional, `SKSE/Plugins/AgeOfSkyrim.dll` cai em `Data`).
2. Nexus → *Manage files* → *Add file*: categoria *Main files*, versão `X.Y.Z`, marque *Mod manager download*.
   Arquivo antigo → *Old versions*.
3. Descrição: cole `docs/nexus/description_en.bbcode` (página principal, em inglês) e acrescente
   `docs/nexus/description_ptbr.bbcode` (aba/spoiler em português). Atualize as seções de funcionalidades com o
   changelog.
4. *Requirements*: SKSE64 e Address Library for SKSE Plugins (obrigatórios); SkyUI e PrismaUI (opcionais).
5. *Changelog* do Nexus: cole a seção da versão do `CHANGELOG.md`.
6. Versão do mod na página = `X.Y.Z`.
