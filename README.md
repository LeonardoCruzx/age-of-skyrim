# Age of Skyrim

Mod de gestão de facções e conquista territorial para Skyrim SE/AE (um único DLL, via SKSE + CommonLibSSE-NG).

**Estado atual — Fase 1 (Core & Banners), esqueleto inicial.** Escrito sem acesso a um compilador MSVC/Skyrim:
as APIs da CommonLibSSE-NG foram conferidas no código-fonte, mas o projeto **ainda não foi compilado nem testado no jogo**.

## Pré-requisitos (Windows)

- Visual Studio 2022 com a carga de trabalho *Desenvolvimento para desktop com C++*
- CMake ≥ 3.21 e Git
- [vcpkg](https://github.com/microsoft/vcpkg) clonado, com a variável de ambiente `VCPKG_ROOT` apontando para ele
- SKSE64 e Address Library for SKSE Plugins instalados no jogo

## Build

```powershell
git clone --recursive <url-do-seu-repo> AgeOfSkyrim
cd AgeOfSkyrim
cmake --preset windows-msvc
cmake --build --preset release
```

Para copiar DLL + JSONs automaticamente para a pasta do mod (MO2/Vortex) a cada build:

```powershell
setx AOS_DEPLOY_DIR "D:\MO2\mods\AgeOfSkyrim"
```

O log fica em `Documents\My Games\Skyrim Special Edition\SKSE\AgeOfSkyrim.log`.

## Como funciona o controle de banners

1. **No Creation Kit** (`AgeOfSkyrim.esp`): em cada forte/torre/vila, coloque um objeto de bandeira **por estilo
   de heráldica** (imperial, stormcloak, ...). Marque cada referência como **Persistent** e **Initially Disabled**.
2. **Em `Data/SKSE/Plugins/AgeOfSkyrim/Territories/*.json`**: declare os estilos e, por território, quais
   referências pertencem a cada estilo (`"Plugin.esp|0xID"`, ID *local* — sem o byte de load order).
3. **Em tempo de execução**: `SetTerritoryOwner(território, estilo)` atualiza o estado (salvo no cosave) e agenda,
   na thread principal, `Enable()` nos banners do dono e `Disable()` em todos os outros. Nenhum script Papyrus.

Por que "estilos" e não "uma bandeira por facção"? Facções criadas pelo jogador são ilimitadas, mas referências
pré-colocadas no CK são finitas. Cada facção escolhe um estilo de um conjunto fechado (os que você modelar/colocar).

### Teste rápido sem UI
`settings.json` → `debug`: a tecla configurada (padrão F10, DIK 68) alterna o dono de `debug.territory`
entre neutro e cada estilo, com notificação na tela.

## Estrutura

| Arquivo | Responsabilidade |
|---|---|
| `src/Core/Ids.h` | IDs por hash FNV-1a (estáveis entre versões do JSON) |
| `src/Core/Config.*` | Leitura dos JSONs, validação, resolução de `Plugin.esp\|0xID` |
| `src/Core/State.*` | Dono de cada território + persistência no cosave (registro `TERR` v1) |
| `src/Banner/BannerController.*` | Único ponto que chama `Enable()/Disable()`; `SetTerritoryOwner()` |
| `src/Events/Events.*` | Retentativa ao carregar células; hotkey de debug |
| `src/Main.cpp` | Entrada SKSE, log, callbacks de cosave e mensagens |

## Pendências conhecidas da Fase 1

- [ ] Compilar e validar no jogo (ver "Pontos a verificar" abaixo)
- [ ] Criação de facção (nome, estilo, cor) com persistência no cosave
- [ ] *Free Claim*: raycast com alinhamento à normal do terreno para estandartes em locais genéricos
- [ ] Ligar a conquista real (captura de local) a `SetTerritoryOwner`

## Pontos a verificar no primeiro teste

- `TESObjectREFR::Enable(false)` / `Disable()` em refs de células descarregadas (esperado: ok para refs persistentes).
- Que o vcpkg baseline do `vcpkg.json` (o mesmo usado pela CommonLibSSE-NG 3.x) resolve `spdlog`/`nlohmann-json` sem conflito.
- Ordem de eventos: `Revert → Load (cosave) → kPostLoadGame → ApplyAll`.
