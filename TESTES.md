# Roteiros de teste

No fim do teste, envie os arquivos da pasta `Documentos\My Games\Skyrim Special Edition\SKSE\`:
`AgeOfSkyrim.log` e `AgeOfSkyrim_territories.json`.

## Instalação (uma vez por versão)

1. No GitHub: **Actions → Build →** o run mais recente da `main` com ✅ **→ Artifacts →** baixe `AgeOfSkyrim-...`.
2. No MO2: *Install a new mod from an archive* → escolha o zip (no Vortex: arraste o zip).
   Ative o mod. Requisitos: SKSE64 e Address Library for SKSE Plugins.
3. Abra o jogo **pelo SKSE** (no MO2, o executável `SKSE`).

---

## Teste único — todas as 28 missões (≈ 35–45 min)

Não precisa de ESP nem de Creation Kit. Tudo roda por hotkey, com `settings.json → debug.enabled: true`
(já é o padrão). O mod cria objetos de teste em runtime; não pegue nenhum objeto que ele criar.

**Antes de começar:** carregue um save e fique num lugar **aberto** (ex.: na estrada, fora de Whiterun — longe
de qualquer forte/torre/cidade de verdade, para não interferir nos passos que usam o território de teste).
Tenha no inventário um pouco de ouro, comida e algum lingote/minério.

A maioria dos passos usa `debug.territory` (uma torre de teste chamada **"test_tower"**) quando você não está
num território de verdade — é por isso que dá para testar tudo parado no mesmo lugar.

### Teclas usadas neste roteiro

| Tecla | Faz o quê | Missão |
|---|---|---|
| **F10** | alterna o dono de `debug.territory` | M2 |
| **F11** | autoteste (bandeiras + facções + acentos + persistência) | M2/M3 |
| **F8** | mostra seu inventário por categoria (e o da sede, se houver) | M7 |
| **F3** | fincar estandarte (Free Claim) | M8 |
| **F4** | simula a conquista do local atual (ou de `debug.territory`) | M5/M6 |
| **F6** | avança `economyHours` horas de economia | M12 |
| **F7** | cria uma rota de comércio de teste | M13 |
| **;** | guarnição do território atual + força o aparecimento dos soldados | M14 |
| **'** | inicia a próxima melhoria disponível | M15 |
| **-** | resumo do reino (doutrina, população, cargos, modificadores) | M18/M19/M22 |
| **=** | simula `realmDebugHours` horas do reino | M18/M19/M22 |
| **[** | troca para a próxima doutrina | M18 |
| **]** | entrega os territórios mais próximos à sua facção | M18 (ajuda a testar M24/M27) |
| **,** | força a avaliação da IA e lista os ataques | M16 |
| **.** | alterna a relação com o dono do território atual | M17 |
| **\\** | oferece um governador seu na capital da província atual | M24 |
| **F1** | simula uma captura de prisioneiros | M25 |
| **F2** | resgata (ouro) o seu 1º prisioneiro | M25 |
| **F12** | executa o seu 1º prisioneiro | M25 |
| **Delete** | estabilidade do território atual + sorteio de rebelião | M26 |
| **Home** | recruta soldados de campo no território atual | M20 |
| **End** | lista os seus exércitos | M20 |
| **Insert** | força uma batalha de teste perto de você | M21 |
| **/** | abre o mapa tático (sem precisar da mesa de guerra) | M11 |

M27 (Domínio de Skyrim) não tem hotkey própria — ela acontece sozinha quando as condições são atingidas;
tem um passo opcional no fim para forçar isso. M9 (API) não tem UI própria; M10 (MCM) é testada à parte,
no bloco opcional "MCM/SkyUI".

---

### A. Autoteste, bandeiras, facções e texto (M2, M3)

1. Aperte **F11**. Deve aparecer **"AoS autoteste OK (...)"** e ficar visível só o objeto da sua facção de
   teste (um sweetroll). Depois aparece **"AoS teste de texto: ação, coração, você, à"** — repare nos acentos.
2. Aperte **F10** algumas vezes: o objeto visível troca (nada → moeda de ouro = Legião Imperial → lingote de
   ferro = Mantos da Tempestade → sweetroll = sua facção). Termine apertando **F11** de novo (volta ao sweetroll).

A partir daqui você tem uma facção própria, **"Minha Facção (teste)"**, que os passos seguintes reaproveitam.

### B. Estoque por categoria (M7)

3. Aperte **F8**: aparece uma linha por categoria do seu inventário ("Ouro: ...", "Comida: ...", ...). Confira
   se o ouro bate com o do inventário.

### C. Free Claim (M8)

4. Aperte **F3** olhando para o chão à frente: surge uma espada de ferro parada no chão. Aperte **F3** de novo
   no mesmo lugar → **"muito perto de outro claim"**. Ande uns 30 m até uma ladeira e aperte **F3**: repare se a
   espada inclina acompanhando o chão (para que lado).

### D. Conquista e sede (M5, M6)

5. Aperte **F4**: `test_tower` é conquistada ("AoS: Torre de teste conquistado por Minha Facção (teste)") e
   aparece a pergunta **"Fundar a sede da sua facção aqui?"** → responda **Sim**. Um baú e uma mesa de guerra
   devem nascer perto de você.
6. Aperte **F8** de novo: agora, além do seu inventário, deve aparecer **"AoS estoque da sede"**.

### E. Economia e rotas (M12, M13)

7. Aperte **F6**: mostra horas processadas, territórios produzindo e itens entregues no baú da sede (agora que
   ela existe, não deve mais aparecer "sem depósito"). Olhe o baú: itens novos devem ter chegado.
8. Aperte **F7**: cria (ou confirma) uma rota de comércio entre dois territórios seus mais próximos.

### F. Guarnição e melhorias (M14, M15)

9. Aperte **;** perto de `test_tower`: mostra o tamanho da guarnição e chama os soldados — eles devem
   aparecer com roupa (não "pelados").
10. Aperte **'**: inicia a próxima melhoria (muralhas). Se faltar material, a mensagem diz o quanto falta
    — coloque esses itens direto no baú da sede (é um baú normal) e aperte de novo. Depois, aperte **F6**
    várias vezes (24h cada) até a obra terminar ("AoS: Muralhas nível I concluída...").

### G. Reino: doutrina, população e títulos (M18, M19, M22)

11. Aperte **-**: mostra a doutrina atual, a população, os cargos e os modificadores.
12. Aperte **[**: troca de doutrina (a 1ª é grátis). Aperte de novo → deve dizer que só pode trocar de
    novo depois de um tempo.
13. Aperte **=** algumas vezes: avança horas do reino (população deve crescer se houver comida) e conta
    a espera da doutrina.
14. Aperte **]**: entrega vários territórios próximos à sua facção, nomeia cargos e governadores onde já
    há maioria de província. Repita (de pontos diferentes, se quiser) até acumular uma província inteira —
    isso também prepara os testes H e M abaixo. Se sua facção subir de título (ex. para "Jarl"), aparece
    **"Minha Facção (teste) agora é Jarl!"**.

### H. Governo local (M24)

15. Viaje até a **cidade** da província onde você tem maioria (do passo 14) e aperte **\\**: pergunta
    **"... é a capital de .... Substituir o governo local por um governador seu?"** → **Sim**. Um NPC deve
    aparecer perto do marcador da cidade; ative-o para ver o status da província (população/estabilidade).

### I. IA e diplomacia (M16, M17)

16. Aperte **,**: força a avaliação da IA (Legião Imperial e Mantos da Tempestade já começam em guerra
    entre si) e lista os ataques em andamento, se houver.
17. Perto de um território que não é seu, aperte **.**: alterna a relação da sua facção com o dono dele
    (paz → guerra → trégua → aliança).

### J. Lei e prisioneiros (M25)

18. Em `test_tower` (que já é sua), aperte **F1**: simula uma captura — como o território já é seu, o teste
    mostra outra facção capturando prisioneiros SEUS (é a direção que o debug usa para exercitar o sistema;
    não é um bug). Aperte **F2** para resgatar (precisa de ouro no baú da sede) e depois **F12** para executar.

### K. Ordem pública e rebeliões (M26)

19. Aperte **Delete**: mostra a estabilidade de `test_tower` e tenta um sorteio de rebelião (pode dizer que o
    sorteio falhou — normal, é uma chance por hora). Se quiser ver uma rebelião de verdade, baixe
    `stability.rebellionThreshold` e `stability.graceHours` no `settings.json` antes de abrir o jogo e repita
    os passos F e K.

### L. Exército e batalhas (M20, M21)

20. Em `test_tower`, aperte **Home**: recruta soldados de campo (gasta ouro/equipamento/população). Depois
    de esperar o treino (use **=** para passar as horas), aperte **End** para listar seus exércitos.
21. Aperte **Insert** perto de um marcador carregado: nasce uma batalha de teste entre dois grupos pequenos de
    facções diferentes. Acompanhe: os soldados devem ter roupa, lutar, e a batalha deve terminar sozinha
    (reforços enquanto um lado tiver força, fim quando um lado chega a zero).

### M. Mapa tático (M11)

22. Ative a mesa de guerra da sede (ou aperte **/** em qualquer lugar): sem PrismaUI instalado, abre um
    menu com o resumo, seus territórios, territórios próximos, rotas e alvos de ataque; marque um território
    como alvo e crie/remova uma rota pelos menus. Com [PrismaUI](https://www.prismaui.dev/) instalado, a mesma
    mesa abre um mapa visual — clique em territórios, crie rotas e marque alvos diretamente nele.

### N. Salvar e carregar

23. **Salve** (save novo) e **carregue esse save**. Deve aparecer **"AoS persistência OK. Teste concluído!"**
    e tudo dos passos anteriores deve continuar como estava (sede, guarnição, melhorias em obra, exércitos,
    rotas, prisioneiros, relações, estabilidade). Confira no log que não aparece nenhuma linha
    **"registro desconhecido"**.

### O. Casas do jogador como sede (M29)

24. Entre numa casa sua que **não** fique dentro de uma cidade (ex.: Lakeview Manor, Windstad Manor, Heljarchen
    Hall) — pode ser em qualquer ponto do teste, não depende dos passos anteriores. Deve aparecer **"AoS: ... é
    sua casa — agora pertence a ..."** e, se você ainda não tem sede, a pergunta **"Fundar a sede da sua facção
    aqui?"**. Se tiver uma casa **dentro** de uma cidade (Breezehome, Hjerim, Honeyside, Vlindrel Hall, Proudspire
    Manor), entre nela também e confirme que **não** aparece nada — ela não deve virar território.

---

## Opcional: Domínio de Skyrim (M27)

M27 não tem hotkey; ela avalia sozinha quando sua facção tem a maioria de `realm.dominationThreshold` (padrão
1.0 = todas) das províncias. Para ver a mensagem sem precisar tomar o mapa inteiro, baixe esse número
temporariamente no `settings.json` (ex. para `0.2`) antes de abrir o jogo, repita o passo **G.14** (**]**)
viajando por 2–3 pontos diferentes do mapa até cobrir essa fração das províncias, e aguarde a próxima
reavaliação (qualquer troca de dono ou **=**). Deve aparecer **"AoS: Minha Facção (teste) dominou
Skyrim!"**. Depois, volte o número ao normal.

## Opcional: MCM / SkyUI (M9, M10)

Precisa do **SkyUI 5.x** instalado e do **`AgeOfSkyrim.esp`** (vem no mesmo artefato) ativado no load order.

24. Abra o menu de configuração do SkyUI: deve aparecer **"Age of Skyrim"**. Veja as páginas Facção, Territórios
    (lista paginada + dono), Sede e Estoque, e Depuração. **Sabido:** a página "Economia e Governo" ainda
    mostra "chega numa versão futura" mesmo com a economia e o reino já funcionando pelos hotkeys — isso é uma
    pendência de UI (a tela não foi atualizada quando a economia e o reino ficaram prontos), não um bug novo.

---

**Se algo der diferente** (mensagem de FALHOU, nenhuma mensagem, objeto errado, crash, uma hotkey sem efeito):
me envie os arquivos e diga em qual passo (letra + número) parou.

**Me diga também:**
- os acentos do passo A.1 apareceram certos?
- para que lado a espada inclinou no passo C.4?
- os soldados do passo F.9 e os da batalha do passo L.21 apareceram vestidos?
- quais nomes de plugin/EditorID o log reclamou que não encontrou (procure por "não encontrado"/"inválido" no
  `AgeOfSkyrim.log")? Vários FormIDs de itens e NPCs vanilla neste mod vêm de memória, não testados até agora.

A descoberta de territórios (M4) roda sozinha ao abrir o jogo e vai para o log + `AgeOfSkyrim_territories.json`.
