# 0067. `FormRef::Resolve<RE::TESForm>()`/`Resolve<RE::TESBoundObject>()` sempre retornavam nulo

- **Status:** Aceita
- **Data:** 2026-10-29
- **Origem:** Bug de usuário (primeiro teste real com log completo, depois dos fixes da ADR 0066)

## Contexto
Com o jogo já abrindo (ADR 0066), o log do primeiro teste mostrou todos os recursos de `Economy.json`
("gold", "bread", "cabbage", "iron", "firewood", "leather") e o `Stock 'gold'` como "não encontrado (plugin
ausente?) ou não é um item", apesar dos FormIDs estarem corretos (confirmados contra a wiki: `Gold001` =
`Skyrim.esm|0x0000000F`, `IngotIron` = `Skyrim.esm|0x0005ACE4`, etc.) — resultado: `Economia: 0 recurso(s),
0 regra(s) de produção` no log, ou seja, a economia (M12) nunca de fato produzia nada. `Garrison.cpp`/
`Troops.cpp` tinham o sintoma análogo (mas mascarado: caem no fallback de "escolha automática por outfit"),
nunca resolvendo os overrides de ator por EditorID do JSON.

Causa: todos os quatro pontos (`Economy.cpp`, `Stock.cpp`, `Garrison.cpp`, `Troops.cpp`) resolvem o form com
`FormRef::Resolve<RE::TESForm>()` (`src/Core/Config.h`), que chama
`RE::TESDataHandler::LookupForm<T>(localId, plugin)`. Esse template (vendorizado,
`extern/CommonLibSSE-NG/include/RE/T/TESDataHandler.h:199-208`) faz:
```cpp
auto form = LookupForm(a_localFormID, a_modName);  // overload sem template, sem filtro
return form->Is(T::FORMTYPE) ? static_cast<T*>(form) : 0;
```
e `RE::TESForm::FORMTYPE = FormType::None` (`TESForm.h:39`) — nenhum form real tem `FormType::None`
(gold é `kMisc`/alquimia, ingot é `kMisc`, etc.), então `form->Is(FormType::None)` é **sempre falso**: o
`form` é encontrado de verdade pelo overload interno sem template, mas o filtro de tipo descarta ele e
devolve nulo de qualquer forma. `Resolve<RE::TESForm>()` nunca funcionou para nenhum form real, em nenhum
mod — não é algo específico desta versão do jogo (ao contrário da ADR 0066).

**Segunda chamada igualmente quebrada, achada no teste seguinte (mesmo log, mais adiante):** com o fix de
`Resolve<RE::TESForm>()` já em teste, `Placement.cpp`/`Claims.cpp` continuaram reportando objeto base/
estandarte não encontrado para os mesmos FormIDs (`Placement: objeto base Skyrim.esm|0xF não encontrado`,
`Claims: objeto do estandarte Skyrim.esm|0x12EB7 não encontrado`), causando o `[autoteste] FAIL criação de
objetos` e `FAIL 0 bandeira(s) resolvida(s)` logo no primeiro autoteste (F11) do usuário. Esses dois usam
`Resolve<RE::TESBoundObject>()`, não `Resolve<RE::TESForm>()` — e `TESBoundObject` (`TESBoundObject.h`) não
redefine `FORMTYPE`; herda de `TESObject` → `TESForm`, chegando no mesmo `FormType::None`. É literalmente o
mesmo bug, só que atrás de um tipo "concreto" diferente que também é, na prática, uma base abstrata sem
`FormType` próprio.

Por que só agora apareceu: eram as únicas chamadas desses dois tipos já exercitadas num teste real;
`Resolve<RE::BGSKeyword>()` (categorias por keyword, M7), `Resolve<RE::BGSOutfit>()`, `Resolve<RE::TESNPC>()`
e `Resolve<RE::BGSLocation>()` usam tipos concretos de verdade (`FORMTYPE` é um `FormType` real, não
`None`) — o filtro do template funciona certo pra esses e sempre funcionou.

## Decisão
Em `FormRef::Resolve()` (`src/Core/Config.h`), antes de chamar `LookupForm<T>`, tratar os dois tipos sem
`FormType` próprio como casos especiais, cada um com o equivalente certo do filtro que o template faria:
- `T = RE::TESForm`: overload **sem template** (`dh->LookupForm(localId, plugin)`), sem filtro nenhum
  (é o "aceita qualquer form").
- `T = RE::TESBoundObject`: mesmo overload sem template, com `form->IsBoundObject()` no lugar do
  `form->Is(FormType)` (é o que `TESBoundObject::IsBoundObject()` sobrescreve para `true`).

Para qualquer outro `T` (tipo concreto, com `FORMTYPE` real — `BGSKeyword`, `BGSOutfit`, `TESNPC`,
`BGSLocation`, ...), mantém `LookupForm<T>` normalmente — o filtro ali é o comportamento certo. Resolvido
num único lugar (`Config.h`), sem tocar os seis chamadores (`Economy.cpp`, `Stock.cpp`, `Garrison.cpp`,
`Troops.cpp`, `Placement.cpp`, `Claims.cpp`).

Alternativa descartada: trocar cada `Resolve<RE::TESForm>()`/`Resolve<RE::TESBoundObject>()` por um tipo
concreto específico em cada chamador (ex.: `TESObjectMISC` pro ouro). Descartada porque `Garrison.cpp`/
`Troops.cpp` resolvem **atores** (`TESNPC`/leveled list) com a mesma chamada de `Resolve<RE::TESForm>()` —
um tipo concreto único não cobre todos os chamadores; o fix genérico no `Resolve()` cobre todos de uma vez
e continua funcionando se mais um chamador usar um desses dois tipos no futuro.

## Consequências
- Economia (M12) passa a produzir de fato: recursos resolvidos, regras de produção carregadas.
- `Stock 'gold'` deixa de falhar; guarnição (M14) e tropas de campo (M20) passam a honrar os overrides de
  ator por `EditorID` do JSON em vez de cair sempre no fallback automático por outfit.
- Nenhuma mudança de cosave; é só correção de leitura de configuração no load.
