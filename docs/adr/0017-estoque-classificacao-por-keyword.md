# 0017. Classificação do estoque por keyword: implementação

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Técnica (detalha a [ADR 0012](0012-estoque-itens-reais.md))

## Contexto
A ADR 0012 decidiu que o estoque da sede é feito de itens reais, contados por categoria via keywords
vanilla. Faltava definir formato de dados, ordem de precedência, custo e como ligar ao baú da sede (M6)
sem que o baú exista ainda.

## Decisão
- **Dados em `Resources.json`**: lista ordenada de categorias (`id`, `name` localizado `{en, pt-br}`), cada uma com
  regras `forms` (form exato, ex. ouro `Skyrim.esm|0x00000F`), `keywords` (`VendorItemFood`, `VendorItemOreIngot`...)
  e `formTypes` (fallback: `Weapon`, `Armor`, `Ammo`...).
- **Precedência por item**: form exato → keyword (a categoria de menor índice entre as que casam) → tipo de form.
  A ordem das categorias no arquivo é a prioridade.
- **Custo**: mapas `keyword → categoria`, `form → categoria` e `tipo → categoria` montados no `kDataLoaded`;
  a classificação de cada objeto base é memorizada (`cache_`), então após a 1ª vez é O(1). A contagem usa
  `GetInventoryCounts()` numa única passada e é medida com `ScopedTimer`.
- **Recontagem só ao fechar o baú**: sink de `MenuOpenCloseEvent` guarda o alvo do `ContainerMenu` ao abrir e,
  ao fechar, reconta apenas se o container estiver no conjunto de **observados**. Sem observados, o sink retorna
  na primeira comparação. A M6 chama `Stock::Observe(baú da sede)`.
- **Verificação**: o log mostra o EditorID de cada keyword resolvida (keywords guardam EditorID no jogo), para
  conferir os IDs do JSON; hotkey de debug (F8) conta o inventário do jogador por categoria.

## Consequências
- Itens de outros mods entram automaticamente se usam as keywords padrão; mods com keywords próprias só
  adicionam linhas no JSON (ou um patch JSON).
- Itens sem keyword nem tipo mapeado aparecem como "Sem categoria" (contados, não descartados).
- Ainda não há evento `OnStockChanged` (M9) nem uso pela economia (M12): `Recount` deixa o TODO.
