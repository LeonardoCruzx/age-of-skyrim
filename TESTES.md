# Roteiros de teste

Cada missão que precisa do jogo tem um roteiro curto aqui. No fim, envie o arquivo de log:
`Documentos\My Games\Skyrim Special Edition\SKSE\AgeOfSkyrim.log`

## Instalação (uma vez por versão)

1. No GitHub: **Actions → Build →** o run mais recente com ✅ **→ Artifacts →** baixe `AgeOfSkyrim-...`.
2. No MO2: *Install a new mod from an archive* → escolha o zip (no Vortex: arraste o zip).
   Ative o mod. Requisitos: SKSE64 e Address Library for SKSE Plugins.
3. Abra o jogo **pelo SKSE** (no MO2, o executável `SKSE`).

---

## M2 + M3 — Bandeiras, facções e salvar/carregar (≈ 5 min)

Não precisa de ESP: o mod cria 3 objetos de teste na sua frente — um por facção:
moeda de ouro = Legião Imperial, lingote de ferro = Mantos da Tempestade, sweetroll = sua facção de teste.

1. Carregue qualquer save e fique num lugar **aberto e plano** (ex.: na estrada, fora de Whiterun).
2. Aperte **F11**. Em cima, à esquerda, deve aparecer: **"AoS autoteste OK (...). Agora salve e carregue."**
   Na sua frente fica visível **só o sweetroll** (sua facção). (Não pegue os objetos.)
   Logo depois aparece **"AoS teste de texto: ação, coração, você, à"** — repare se os acentos aparecem certos.
3. Aperte **F10** algumas vezes: a cada toque, o objeto visível muda
   (nada → moeda → lingote → sweetroll → ...) e aparece "AoS: Torre de teste -> <facção>".
4. Aperte **F11** de novo (volta ao sweetroll), depois **salve** (save novo) e **carregue esse save**.
   Deve aparecer: **"AoS persistência OK. Teste concluído!"**
5. Feche o jogo e me envie o `AgeOfSkyrim.log` + diga se os **acentos** do passo 2 apareceram certos.

**Se algo der diferente** (mensagem de FALHOU, nenhuma mensagem, objeto errado, crash): só me envie o log
e diga em qual passo parou.

---

## M7 (núcleo) — Estoque por categoria (≈ 2 min)

1. Carregue um save em que você tenha comida, ouro e algum lingote/minério no inventário.
2. Aperte **F8**. Aparece **"AoS estoque (seu inventário):"** e uma linha por categoria
   (ex.: "Ouro: 350", "Comida: 12", "Metais: 4"...) e por último "Sem categoria: ...".
3. Confira se os números fazem sentido (ex.: o ouro bate com o ouro do inventário).
4. Me envie o `AgeOfSkyrim.log` (ele lista o nome de cada keyword usada, para eu conferir os IDs).

## M8 — Free Claim (≈ 3 min)

1. Num lugar **aberto** (fora de cidades/cavernas), olhe para a frente e aperte **F9**.
   Deve aparecer **"AoS: estandarte fincado (1 de 10, ...)"** e uma **espada de ferro** surge no chão à sua frente,
   parada (não cai nem rola). Não pegue a espada.
2. Aperte **F9** de novo no mesmo lugar: deve aparecer **"muito perto de outro claim"**.
3. Vá até uma **ladeira** (afaste-se uns 30 m), olhe morro acima e aperte **F9**: a espada deve ficar
   **inclinada acompanhando o chão**. Repare se ela inclina para o lado certo ou ao contrário.
4. **Salve**, **carregue** o save e confira que as espadas continuam lá.
5. Me envie o `AgeOfSkyrim.log` + diga como ficou a inclinação do passo 3.
