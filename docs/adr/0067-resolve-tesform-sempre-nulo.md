# 0067. `FormRef::Resolve<RE::TESForm>()` sempre retornava nulo (economia/estoque/guarnição/tropas)

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

Por que só agora apareceu: essa era a única chamada do tipo `Resolve<RE::TESForm>()` que já tinha sido
exercitada num teste real; `Resolve<RE::BGSKeyword>()` (categorias por keyword, M7) e `Resolve<RE::TESBoundObject>()`
diretos (baú/mesa da sede, M6) usam tipo concreto — `T::FORMTYPE` é o tipo real nesses casos, então o filtro
funciona certo e sempre funcionou.

## Decisão
Em `FormRef::Resolve()` (`src/Core/Config.h`), antes de chamar `LookupForm<T>`, tratar `T = RE::TESForm`
como caso especial: usar o overload **sem template** (`dh->LookupForm(localId, plugin)`), que devolve o
`TESForm*` bruto sem o filtro de `FormType`. Para qualquer outro `T` (tipo concreto), mantém
`LookupForm<T>` normalmente — o filtro ali é o comportamento certo (rejeitar forms do tipo errado).
Resolvido num único lugar (`Config.h`), sem tocar os quatro chamadores.

Alternativa descartada: trocar `Resolve<RE::TESForm>()` por `Resolve<RE::TESBoundObject>()` em cada
chamador. Descartada porque `Garrison.cpp`/`Troops.cpp` resolvem **atores** (`TESNPC`/`TESLevCharacter`,
não bound objects) com a mesma chamada — um tipo concreto único não cobre os quatro casos; o fix genérico
no `Resolve()` cobre todos de uma vez e continua funcionando se um quinto chamador aparecer.

## Consequências
- Economia (M12) passa a produzir de fato: recursos resolvidos, regras de produção carregadas.
- `Stock 'gold'` deixa de falhar; guarnição (M14) e tropas de campo (M20) passam a honrar os overrides de
  ator por `EditorID` do JSON em vez de cair sempre no fallback automático por outfit.
- Nenhuma mudança de cosave; é só correção de leitura de configuração no load.
