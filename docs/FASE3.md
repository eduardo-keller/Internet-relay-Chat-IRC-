# FASE 3 — Comandos contra o irssi (plano de execução)

Plano passo a passo da Fase 3 descrita em [PLANO.md](PLANO.md) §3. O contrato
técnico continua sendo [ARCHITECTURE.md](ARCHITECTURE.md); a lista de itens
continua sendo [TASKS.md](TASKS.md). **Este arquivo é o rastreador do dia a dia
da Fase 3** — atualize o status no mesmo commit que muda o código.

Branch: `fase_3`.

**Status:** `todo` → `doing` → `done` (funciona e tem teste) → `integrated`
(mergeado na `main`).

> **Mudança de dono.** O `TASKS.md` atribui os 17 itens desta fase à trilha
> DOMAIN (colega). **Eduardo assume a fase inteira** — os sete handlers, os
> testes e a convergência com o irssi. Os nomes de arquivo reservados ao DOMAIN
> no `FASE2.md` §3.3 continuam valendo (`src/CommandsChannel.cpp`), para que o
> `TASKS.md` continue legível e um eventual retorno do colega não colida.

---

## 1. Pré-requisitos — verificados em 2026-08-24

Nada aqui é promessa: cada linha foi conferida no código antes de escrever este
plano.

| O que a Fase 3 precisa | Onde está | Estado |
|---|---|---|
| `make` limpo com `-Wall -Wextra -Werror -std=c++98` | `Makefile` | ok |
| `make test` verde | 395 asserções, 0 falhas | ok |
| Modelo de `Channel` completo (membros, ops, convites, `i`/`t`/`k`/`l`, `modeString`) | `src/Channel.cpp`, 29 funções | ok |
| `findChannel` / `getOrCreateChannel` / `removeChannel` **case-insensitive** (D5) | `src/ServerChannels.cpp` | ok |
| `broadcastToChannel` com `except` | `src/ServerChannels.cpp` | ok |
| `broadcastToPeers` deduplicado | `src/ServerChannels.cpp` | ok |
| `findClientByNick` case-insensitive, ignora quem está saindo | `src/Server.cpp` | ok |
| `sweepChannels` tira o cliente de membros/ops/convites e apaga canal vazio | `src/ServerChannels.cpp` | ok |
| Porta de registro (`451`) e comando desconhecido (`421`) no despachante | `Server::handleLine` | ok |
| `Replies::numeric` / `fromClient`, sem CRLF | `src/Replies.cpp` | ok |
| Todos os numerics da fase declarados (324, 331, 332, 341, 353, 366, 401, 403, 404, 411, 412, 441, 442, 443, 461, 471, 472, 473, 475, 482) | `include/Replies.hpp` | ok |
| `utils::split` preservando campo vazio (`JOIN #a,#b ,key2`) | `src/Utils.cpp` | ok |
| `utils::isValidChannelName` (`#`, 2..50, sem espaço/vírgula/`:`) | `src/Utils.cpp` | ok |
| `utils::parseInt` que recusa em vez de chutar (`MODE +l`) | `src/Utils.cpp` | ok |
| Truncagem de saída em 510 no ponto único (`sendToClient`) | `src/Server.cpp` | ok |
| `valgrind` instalado | `/usr/bin/valgrind` | ok |
| **irssi instalado** | — | **AUSENTE — Passo 0** |

**Conclusão: a Fase 3 está destravada.** Não falta nenhuma peça de arquitetura;
falta o cliente de referência e os sete handlers.

### 1.1 Quatro armadilhas que o código atual já tem plantadas

Encontradas na leitura, não na execução. Cada uma tem um passo dono.

1. **`tests/test_server.cpp` quebra no primeiro `JOIN` registrado.**
   `testCommandTable()` afirma hoje `table.find("JOIN") == table.end()` e
   `table.size() == 7`. São asserções sobre o estado *atual*, propositais — e
   viram falha no minuto em que a primeira entrada do DOMAIN é descomentada.
   **Dono: cada passo que registra um comando atualiza essas duas linhas no
   mesmo commit.**

2. **`tests/it/channel_seam.sh` deixa de se auto-pular e passa a falhar.**
   Ele sonda `JOIN #probe` numa conexão **não registrada** e pula se a resposta
   contiver `421`. Com `JOIN` na tabela a resposta vira `451` (porta de
   registro), o `SKIP` não dispara, e os clientes do script — que nunca mandam
   `PASS`/`NICK`/`USER` — nunca entram em canal nenhum. **Dono: Passo 1**, que
   reescreve o script com registro completo antes do `JOIN`.

3. **Teste de unidade não alcança `Server::_clients`.** Cliente só entra nesse
   mapa por `accept()`, e abrir uma porta dos fundos no seam estragaria
   justamente o que está sendo testado (é a mesma razão pela qual o `433` mora
   em `tests/it/registration.sh`). Consequência prática: `findClientByNick`
   sempre devolve `NULL` em teste de unidade. O caminho **feliz** de
   `PRIVMSG <nick>` e de `INVITE` só é demonstrável em script de integração; o
   caminho de erro (`401`) é testável em unidade. Ver **D10**, que remove esse
   problema de `KICK` e de `MODE +o` por desenho.

4. **A ordem do `353` não é determinística.** `Channel::getMembers()` devolve
   `std::set<Client *>` — a ordem é a dos endereços na memória, que muda entre
   execuções (`FASE2.md` §3.3, item 7). **Nenhuma asserção pode comparar a
   linha do `353` inteira.** Testa-se presença de cada nick, contagem, e o
   prefixo `@` no operador.

5. **Dois testes verdes hoje afirmam o comportamento errado do `CAP`** (D17).
   `tests/test_cmd_session.cpp::testPongAndCapAreSilent` e
   `tests/it/session.sh` linha 88 ("CAP LS 302 nao gera resposta nem erro")
   travam justamente o silêncio que impede o irssi de registrar. São o exemplo
   perfeito do aviso do `ARCHITECTURE.md` §9: *um parser pode passar em todo
   teste que escrevemos para ele e ainda assim discordar do irssi.* **Dono:
   Passo 0.5.**

---

## 2. Decisões desta fase

Continuam a numeração do `FASE2.md` (D1–D7).

| # | Decisão | Valor | Status |
|---|---|---|---|
| D8 | `341 RPL_INVITING` | `<nick> <channel>` — a ordem do **RFC 1459**, que é a que os servidores reais mandam, e não a do RFC 2812. **Muda a tabela do `ARCHITECTURE.md` §6**, que hoje traz a forma 2812 com a nota "escolha uma e verifique no irssi" | aceita 2026-08-24 |
| D9 | `366 RPL_ENDOFNAMES` | `End of /NAMES list` — **com barra**, a forma difundida no software implantado. Mesma nota, mesma tabela | aceita 2026-08-24 |
| D10 | Como `KICK` e `MODE +o` resolvem o alvo | **Varrendo os membros do canal por nick** (`utils::equalsIgnoreCase`), **não** por `findClientByNick`. Dois ganhos: (a) casa com o RFC 2812, que para `KICK` lista `441 ERR_USERNOTINCHANNEL` e **não** lista `401`; (b) torna o caminho feliz testável em unidade, porque não depende de `Server::_clients` — ver §1.1 item 3. `findClientByNick` fica só onde o alvo pode não ser membro: `INVITE` e `PRIVMSG <nick>` | aceita 2026-08-24 |
| D11 | Arquivo dos handlers de canal | `src/CommandsChannel.cpp`, o nome reservado ao DOMAIN no `FASE2.md` §3.3. Se passar de ~400 linhas, divide-se em `src/CommandsChannel.cpp` (JOIN/PART/PRIVMSG/TOPIC) e `src/CommandsMode.cpp` (KICK/INVITE/MODE) — o `Makefile` usa `wildcard`, então não precisa de edição | aceita 2026-08-24 |
| D12 | Parâmetro ruim de modo | **Ausente** em `+k`/`+l`/`+o` → `461 ERR_NEEDMOREPARAMS`. **Presente mas inválido** em `+l` (não numérico, ou `<= 0`) → a flag é **ignorada em silêncio**, sem numeric: não existe código na tabela para "parâmetro absurdo", e inventar um é proibido pelo `ARCHITECTURE.md` §10 | aceita 2026-08-24 |
| D13 | `PRIVMSG` para canal de quem não é membro | `404 ERR_CANNOTSENDTOCHAN`. Não implementamos `+n`, mas deixar um estranho falar num canal em que não está é pior do que recusar, e o `404` já está na tabela | aceita 2026-08-24 |
| D14 | `JOIN 0` (sair de todos os canais) | **Não implementado.** `0` reprova em `isValidChannelName` e ganha `403 ERR_NOSUCHCHANNEL`. Está fora da lista do subject, e o irssi manda `PART` explícito | aceita 2026-08-24 |
| D15 | `WHO` / `WHOIS` / outros que o irssi mandar sozinho | **CORRIGIDA no Passo 1 — a primeira versão estava errada.** O irssi 1.4.5 manda sozinho: `CAP LS 302`, **`JOIN :`** (lista vazia, antes até do `CAP END`), `CAP END`, `PASS`, `NICK`, `USER`, `MODE <nick> +i`, `MODE <canal>` após cada `JOIN`, `PING` no timer — **e também `WHO <canal>` e `WHOIS <nick>`**. A captura do Passo 0 tinha **um cliente só e um canal vazio**, e nessas condições o `WHO`/`WHOIS` não aparece; com dois clientes num canal, aparece, e vira `421 :Unknown command` na janela de status dos dois. Uma amostra de um cliente não descreve o comportamento de dois — a lição a levar para o Passo 13. Tratamento pendente: **Passo 1.5** | corrigida 2026-08-24 |
| D18 | `JOIN` com lista de canais **vazia** (`JOIN :`) | **Silêncio**, sem numeric. O irssi manda essa linha sozinho em toda conexão — confirmado com perfil recém-criado, e visível também na captura contra o servidor-mock, então não é resíduo de configuração. `403` ou `461` ali poriam uma linha de erro na janela de status de todo mundo que conecta, que é o que o subject proíbe. **`JOIN` sem parâmetro nenhum continua `461`**: essa forma nenhum cliente manda, e é erro de quem digita no `nc`. A assimetria é medida, não inventada | aceita 2026-08-24 |
| D16 | Tamanho de tópico | Sem constante nova em `Limits.hpp`. O único teto é a truncagem em 510 do `sendToClient`, que já cobre o caso do prefixo empurrar a linha para fora do limite | aceita 2026-08-24 |
| D17 | **`CAP` tem de responder — revoga a D2** | O irssi 1.4.5 **bloqueia o registro** esperando resposta ao `CAP LS`: com o handler silencioso da D2 ele nunca manda `PASS`/`NICK`/`USER` e o cliente de referência jamais conecta. `cmdCap` passa a responder `:ircserv CAP * LS :` (lista de capacidades **vazia**), ao que o irssi responde `CAP END` e segue o registro normalmente. `CAP REQ` → `NAK`; `CAP END` e qualquer outro subcomando → silêncio. A **motivação** da D2 continua de pé (nada de `421`, que polui a janela de status); o que estava errado era achar que silêncio bastava | aceita 2026-08-24, medida |

---

## 3. Visão geral dos passos

| # | Passo | Entrega observável | Status |
|---|---|---|---|
| 0 | irssi de verdade + captura do fio | D15 resolvida; **D2 refutada** | **done** 2026-08-24 |
| 0.5 | `cmdCap` responde ao `CAP LS` (D17) | **o irssi completa o registro** — bloqueia toda a fase | **done** 2026-08-24 |
| 0.6 | `cmdMode` mínimo: silêncio em alvo não-canal, consulta `324` | some o `MODE Unknown command`; sobra o `JOIN :` (D18, Passo 1) | **done** 2026-08-24 |
| 1 | `cmdJoin` — canal novo, `JOIN`/`332`/`353`/`366` | `/join #test` abre a janela **com a lista de nicks** | **done** 2026-08-24 |
| 1.5 | `WHO` / `WHOIS` — o `421` que só aparece com dois clientes | janela de status limpa numa sessão de duas pessoas | todo |
| 2 | `cmdJoin` — múltiplos canais e portões `+i`/`+k`/`+l` | `#a,#b key1,key2`; `473`/`475`/`471` | todo |
| 3 | `cmdPart` | dois irssi, um sai, o outro vê; canal vazio some | todo |
| 4 | `cmdPrivmsg` para canal | conversa entre dois irssi | todo |
| 5 | `cmdPrivmsg` para usuário | `/msg bob oi` abre janela no bob | todo |
| 6 | `cmdTopic` | `/topic` mostra e altera; `+t` bloqueia | todo |
| 7 | `cmdKick` | `/kick` tira o alvo da janela dele | todo |
| 8 | `cmdInvite` | `/invite` entra em canal `+i`; **confirma D8** | todo |
| 9 | `cmdMode` — parser, consulta `324`, `472` | `/mode #c` mostra os modos | todo |
| 10 | `cmdMode` — `i` e `t` | `+i` fecha o canal, `+t` tranca o tópico | todo |
| 11 | `cmdMode` — `k` e `l` | chave e limite ligam e desligam | todo |
| 12 | `cmdMode` — `o` | `/op` e `/deop` entre dois irssi | todo |
| 13 | Convergência final e fechamento | dois irssi, valgrind limpo, docs atualizados | todo |

---

## 4. Ciclo obrigatório em cada passo

O mesmo da Fase 1 do DOMAIN, porque funcionou:

1. **Teste primeiro.** Escreva as asserções do passo em
   `tests/test_cmd_channel.cpp` e rode `make test` — elas têm que **falhar**
   (ou nem linkar) antes de existir implementação. Um teste que nunca viu o
   próprio bug é um chute (`ARCHITECTURE.md` §9).
2. **Implementação mínima** do passo, e só dela.
3. `make test` verde, e `make` sem nenhum warning.
4. **Prova de protocolo**: o script `tests/it/*.sh` do passo, com bytes de
   verdade. `make test` prova consistência interna, não conformidade.
5. **Prova no irssi** quando o passo tem efeito visível nele.
6. `TASKS.md` e este arquivo atualizados **no mesmo commit** que muda o código.
7. Commit pequeno, mensagem em inglês, imperativo.

Regra de ouro que vale para todos os passos: **nenhum handler vê um fd, nenhum
handler chama `send`, e nenhum handler normaliza nome de canal** — passa o que
o cliente mandou para o seam e ele resolve (D5).

---

## 5. Passo 0 — irssi de verdade contra o servidor que já existe

**Executado em 2026-08-24. Nenhuma linha de C++ escrita — e mesmo assim é o
passo mais valioso da fase até aqui, porque refutou uma decisão da Fase 2.**

### Como foi feito

`irssi 1.4.5` instalado. O `/rawlog` do irssi **saiu vazio** (ele não se
prende a um servidor que ainda não completou o registro), então a captura foi
feita no nível do fio, que é evidência melhor: um proxy TCP de 40 linhas em
`python3` entre o irssi e o `ircserv`, gravando os dois sentidos com
timestamp. O irssi foi dirigido dentro de um `tmux` com `--home` isolado, sem
tocar na configuração pessoal.

Os dois scripts são descartáveis e vivem fora do repositório (scratchpad da
sessão), como manda o `PLANO.md` §4.

### O que o fio mostrou — contra o nosso servidor

```
C->S  CAP LS 302
      (silêncio: cmdCap é no-op, decisão D2)
```

**E acabou aí.** O irssi imprime `Waiting for CAP LS response...`, **nunca
manda `PASS`/`NICK`/`USER`**, e a conexão morre sem registro. O servidor
registrou o `accept` e nada mais. A D2 acertou o motivo (`421` polui a janela
de status) e errou o remédio: silêncio não basta. Ver **D17**.

### O que o fio mostrou — contra um mock que responde ao `CAP`

Um servidor-mock em `python3` respondendo `:mock CAP * LS :` (lista vazia)
destrava tudo, e de quebra revela o resto do comportamento espontâneo do
irssi:

```
C->S  CAP LS 302
S->C  :mock CAP * LS :
C->S  CAP END
C->S  PASS secret
C->S  NICK edu_k
C->S  USER edu_k edu_k 127.0.0.1 :edu_k
S->C  001 002 003 004
C->S  MODE edu_k +i          <- MODE de USUÁRIO, sozinho, ~2 s depois
C->S  JOIN #test
S->C  :edu_k!u@127.0.0.1 JOIN #test
S->C  332 / 353 / 366
C->S  MODE #test             <- consulta de modo de CANAL, sozinha, após cada JOIN
```

Nenhum `WHO`, nenhum `WHOIS`. É isso que fecha a **D15**.

### O terceiro achado: a sequência do §6 está certa

O mock respondeu ao `JOIN` exatamente com a sequência que o
`ARCHITECTURE.md` §6 prescreve, e o irssi renderizou tudo:

```
-!- edu_k [u@127.0.0.1] has joined #test
-!- Topic for #test: o topico de teste
[Users #test]
[@edu_k] [ outro]
-!- Irssi: #test: Total of 2 nicks [1 ops, 0 halfops, 0 voices, 1 normal]
```

O `@` do `353` virou "1 ops" na contagem do irssi, e o `366` com barra (**D9**)
foi aceito sem reclamação. **O Passo 1 já sabe que o alvo dele está certo antes
de ser escrito** — que era exatamente o objetivo de fazer o Passo 0 primeiro.

### Pronto quando

Cumprido: D15 respondida com evidência, D17 registrada, e o Passo 1 com o
formato de saída validado contra o cliente real. **O critério "o irssi conecta
sem erro" só será atingível depois do Passo 0.5** — hoje ele não conecta.

---

## 5.1 Passo 0.5 — `cmdCap` responde ao `CAP LS` (D17)

**Bloqueia a fase inteira.** Enquanto isto não entrar, nenhum passo pode ser
verificado no cliente de referência: o irssi não passa do `CAP`.

É trabalho da trilha **TRANSPORT**, não do DOMAIN — uma correção da Fase 2
descoberta na Fase 3. Vale um commit isolado, com esse enquadramento na
mensagem.

### Implementar

Em `src/CommandsRegistration.cpp`, onde `cmdCap` já mora:

| Subcomando | Resposta |
|---|---|
| `CAP LS` (com ou sem `302`) | `:ircserv CAP * LS :` — lista **vazia** |
| `CAP REQ :<x>` | `:ircserv CAP * NAK :<x>` — não temos nada a conceder |
| `CAP END` | silêncio |
| qualquer outro, ou sem parâmetro | silêncio |

O alvo é `*`, não o nick: o cliente ainda não tem um. A linha é montada à mão,
como o `PONG` do `cmdPing` — não é numeric, então `Replies::numeric` não serve.
`CAP` continua na lista de comandos liberados antes do registro.

### Testes de unidade — `tests/test_cmd_session.cpp`

`testPongAndCapAreSilent` **se divide em dois**: `PONG` continua silencioso;
`CAP` ganha teste próprio.

```
CAP LS 302   -> ":ircserv CAP * LS :\r\n"
CAP LS       -> idem (o 302 é opcional)
CAP REQ :multi-prefix -> ":ircserv CAP * NAK :multi-prefix\r\n"
CAP END      -> ""
CAP          -> ""   (sem parâmetro, não pode quebrar)
CAP nada disso -> ""
```

### Integração — `tests/it/session.sh`

A linha 88 inverte de sentido: de "não gera resposta" para a resposta exata.
Adicionar um caso que é o **bug real** em forma de teste — a sequência
completa do irssi num pacote só:

```
CAP LS 302\r\nCAP END\r\nPASS secret\r\nNICK alice\r\nUSER alice 0 * :Alice\r\n
```

tem que devolver o `CAP ... LS` **e** os quatro numerics do registro.

### Prova no cliente real — feita em 2026-08-24

O fio, capturado com o mesmo proxy do Passo 0, agora contra o **nosso**
servidor:

```
C->S  CAP LS 302
S->C  :ircserv CAP * LS :
C->S  CAP END
C->S  PASS secret
C->S  NICK edu_k
C->S  USER edu_k edu_k 127.0.0.1 :edu_k
S->C  :ircserv 001 edu_k :Welcome to the Internet Relay Network edu_k!edu_k@127.0.0.1
S->C  :ircserv 002/003/004
C->S  MODE edu_k +i
S->C  :ircserv 421 edu_k MODE :Unknown command      <- sobra para o Passo 0.6
C->S  PING ircserv
S->C  :ircserv PONG ircserv :ircserv
C->S  QUIT :tchau
S->C  ERROR :tchau
```

**Este é o item 1 do `PLANO.md` §3 Fase 3** ("registrar no irssi"), dado como
pronto na Fase 2 sem nunca ter sido testado no irssi, e só agora cumprido.

De brinde, dois itens da Fase 2 que também nunca tinham visto o cliente real
passaram na mesma sessão: o **`PING` do timer do irssi** recebeu o `PONG`
correto, e o **`QUIT`** fechou limpo (`ERROR :tchau`, e o servidor logando
`closing fd 4`). Continuam sem verificação no irssi: `433`, `464` e o
`NICK` de troca — Passo 13.

### Resultado

- `make test`: **402 asserções, 0 falhas** (eram 395 + 7 do `CAP`);
- `tests/it/session.sh`: **14 passed, 0 failed**, sob valgrind, sem vazamento e
  sem leitura inválida;
- irssi 1.4.5: registra, e a única linha de erro que sobra na janela de status
  é o `MODE Unknown command` do Passo 0.6.

### Documentos no mesmo commit

- `ARCHITECTURE.md` §7, item 1 da lista "o que o irssi realmente faz": trocar
  "ignore it silently" pela resposta da D17, mantendo o motivo;
- `TASKS.md`, Fase 2: a linha `CAP não derruba o servidor` diz hoje
  "sem 421 e sem resposta" — vira "responde `CAP * LS :` (D17, corrige D2)";
- `FASE2.md` §2: marcar a D2 como **superada pela D17**, sem apagá-la. A
  decisão errada e o motivo de ter sido tomada valem mais na avaliação do que
  um histórico limpo.

---

## 5.2 Passo 0.6 — `cmdMode` mínimo: calar o `421` que sobra

O Passo 0.5 destrava o registro e, com isso, **expõe o erro seguinte**. O fio
do Passo 0 mostra que o `MODE` do irssi não depende de entrar em canal nenhum:

```
 5.06  C->S  USER edu_k edu_k 127.0.0.1 :edu_k
 5.06  S->C  001 002 003 004
 7.52  C->S  MODE edu_k +i        <- 2,4 s depois do registro, sem nenhum JOIN
11.02  C->S  JOIN #test
13.53  C->S  MODE #test
```

`MODE` não está na `CommandTable`, e o despachante responde `421` a comando
desconhecido **antes** da porta de registro (`Server::handleLine`). Então, com
só o 0.5, um `/connect` puro imprime `MODE Unknown command` na janela de
status — o mesmo `JOIN Unknown command` que a primeira captura mostrou lá.
Trocar um bloqueio por um erro visível é progresso, mas não é o critério do
subject cumprido.

### Implementar

Cria `src/CommandsChannel.cpp` (D11) mais cedo do que o previsto, com um
`cmdMode` que **não altera modo nenhum**:

- `461` sem parâmetro;
- **alvo que não começa com `#` → silêncio absoluto, sem numeric** (D15).
  Responder `403 :No such channel` ao `MODE <nick> +i` poria um erro na janela
  de status de todo mundo que conecta — a armadilha da D2 por outro caminho.
  Medido no Passo 0: sem resposta, a janela fica limpa;
- `403` se o canal não existe;
- **consulta** (só o nome do canal, sem string de modos):
  `324 RPL_CHANNELMODEIS` com `channel->modeString(...)`. A chave só é
  revelada a membros — é para isso que serve o `bool includeParams`;
- **qualquer tentativa de alteração**: fica para os Passos 9 a 12. Até lá,
  silêncio (não `472`, que é para flag desconhecida, e não `482`, que exige um
  julgamento que ainda não sabemos fazer).

Descomentar `table["MODE"] = &cmdMode;`.

### Testes de unidade — `tests/test_cmd_channel.cpp` (arquivo novo)

É este passo, e não o Passo 1, que cria o arquivo de teste e as duas linhas do
`harness.hpp` / `test_main.cpp`.

```
MODE sem parâmetro          -> 461 ... MODE :Not enough parameters
MODE edu_k +i               -> "" (silêncio — o caso real do irssi)
MODE edu_k                  -> "" (idem, sem string de modos)
MODE #nope                  -> 403
consulta em canal sem modo  -> 324 ... #c +
consulta em canal +itk por MEMBRO      -> a chave aparece
consulta em canal +itk por NÃO-membro  -> a chave NÃO aparece
```

E a manutenção que todo passo que registra comando carrega:
`tests/test_server.cpp::testCommandTable` vai de `7` para `8` entradas, com
`MODE` presente.

### Pronto quando — e o que este passo NÃO alcança

**Concluído em 2026-08-24.** `make test` em 415 asserções, `tests/it/mode.sh`
com 8 casos, e o fio mostrando `MODE edu_k +i` **sem nenhuma resposta**: a
linha `MODE Unknown command` sumiu da janela de status.

O script foi **validado por mutação**, como os dois do `FASE2.md`: trocando o
`return` do ramo silencioso por uma queda no caminho do `403` — que é
exatamente o erro tentador —, três asserções falham, a do `403` incluída.

> **O critério que este passo escrevia para si mesmo era otimista demais.** Ele
> dizia "`/connect` e `/join` sem uma linha de erro". Sobra uma: o irssi manda
> **`JOIN :`** sozinho, e sem `cmdJoin` isso ainda é `421 :Unknown command`.
> Descoberto ao verificar com um perfil de irssi recém-criado, justamente para
> não afirmar limpeza sem ter olhado. Quem fecha isso é o **Passo 1**, com a
> **D18**. O que o 0.6 entrega é uma das duas linhas de erro, e a certeza de
> qual é a outra.

---

## 6. Passo 1 — `cmdJoin`: canal novo e a sequência de entrada

A fatia mais importante da fase. Tudo depois dela depende de conseguir entrar
num canal.

### Implementar

Em `src/CommandsChannel.cpp` (criado no Passo 0.6), `cmdJoin` cobrindo
**apenas um canal, sem chave e sem modos**:

- `461` se não veio parâmetro **nenhum**;
- **`JOIN :` — um único parâmetro vazio — é silêncio (D18)**, não `403`. O
  irssi manda essa linha sozinho em toda conexão, antes mesmo do `CAP END`;
- `403` se `!utils::isValidChannelName(nome)` (cobre `&foo`, `foo`, `#`, `0`);
- `getOrCreateChannel` — o seam já é case-insensitive, não normalize nada;
- se o canal foi **criado agora** (estava vazio antes do `addMember`), o
  entrante vira operador: `addOperator`;
- já é membro → não faz nada, sem numeric (re-`JOIN` é silencioso);
- `addMember`, e então a sequência **nesta ordem** (`ARCHITECTURE.md` §6):

```
:nick!user@host JOIN #chan            broadcast a TODOS os membros, o entrante incluído
:ircserv 332 nick #chan :<topico>     ou 331 se não há tópico
:ircserv 353 nick = #chan :@alice bob
:ircserv 366 nick #chan :End of /NAMES list     (D9)
```

O `@` do `353` sai de `Channel::isOperator`. Não implementamos voice, então
nunca há `+`.

Descomentar `table["JOIN"] = &cmdJoin;` em `src/CommandTable.cpp`.

### Testes de unidade — `tests/test_cmd_channel.cpp` (arquivo novo)

Adicionar `void runCommandChannelTests(void);` em `tests/harness.hpp` e a
chamada em `tests/test_main.cpp` — as duas linhas do `FASE2.md` §3.3 item 4.

```
JOIN sem parâmetro                     -> 461 ... JOIN :Not enough parameters
JOIN :          (parâmetro vazio)      -> "" silêncio (D18, o caso do irssi)
JOIN &foo / JOIN foo / JOIN #          -> 403 ... :No such channel
JOIN #room num servidor vazio          -> o canal passa a existir (findChannel != NULL)
                                       -> o entrante é membro E operador
                                       -> a saída dele contém, nesta ordem: JOIN, 331, 353, 366
331 quando não há tópico; 332 quando setTopic foi chamado antes
353 traz "@" no operador e nenhum "@" no segundo membro
353 contém TODOS os nicks (checar um a um — a ordem é indeterminada, §1.1 item 4)
o segundo a entrar NÃO vira operador
o segundo a entrar recebe o JOIN do primeiro? não: recebe o próprio, e o primeiro
  recebe o JOIN do segundo (broadcast inclui o remetente)
JOIN #Room depois de JOIN #room        -> mesmo canal, não cria um segundo (D5)
JOIN repetido no mesmo canal           -> não duplica membro, não reenvia a sequência
```

### Consertar o que este passo quebra

- `tests/test_server.cpp::testCommandTable` — trocar
  `check(table.find("JOIN") == table.end(), ...)` por a afirmação inversa e
  `table.size() == 7` por `8`.
- `tests/it/channel_seam.sh` — a sonda e os dois clientes passam a fazer
  `PASS secret\r\nNICK x\r\nUSER x 0 * :X\r\nJOIN #room\r\n`. A condição de
  `SKIP` some: o `JOIN` agora existe. É o próprio comentário do script
  antecipando este dia.

### Integração — `tests/it/join.sh` (novo)

Dois clientes registrados entram em `#room`; asserções sobre os bytes:
sequência `JOIN`/`332`ou`331`/`353`/`366`, `@` no criador, e o segundo `JOIN`
chegando ao primeiro cliente.

### Pronto quando — cumprido em 2026-08-24

`make test` em **446 asserções**, `tests/it/join.sh` com 15 casos, e o
`channel_seam.sh` **rodando de verdade** pela primeira vez (9 casos, sob
valgrind, sem vazamento e sem leitura inválida) — ele passou a Fase 2 inteira
se auto-pulando à espera deste handler.

No irssi, com **duas** sessões: a janela do canal abre com a lista de nicks
preenchida (`[@edu_k] [ edu_k_]`, "Total of 2 nicks [1 ops]"), e a entrada do
segundo é anunciada ao primeiro.

Três coisas que apareceram só por ter usado dois clientes de verdade:

1. **O `433` funciona no cliente real.** O segundo irssi pediu o mesmo nick,
   levou `433` e se renomeou sozinho para `edu_k_`. Item da Fase 2 que nunca
   tinha sido verificado fora do `nc`.
2. **`WHO` e `WHOIS` existem**, ao contrário do que a D15 dizia. Ver o Passo
   1.5.
3. **Uma linha duplicada no fio não era bug.** O `JOIN` do segundo cliente
   aparece duas vezes no log do proxy porque ele atende as duas conexões e
   grava tudo no mesmo arquivo: uma cópia é o eco para quem entrou, a outra é
   o broadcast para quem já estava. O `broadcastToChannel` mandou uma por
   socket, que é o correto.

### O que este passo consertou de quebra

- `tests/it/channel_seam.sh` deixou de sondar por `421` e passou a registrar
  antes do `JOIN`. Duas asserções novas provam a varredura de verdade: a
  entrante depois da morte recebe o `366`, e a morta **sumiu** do `353`.
- Nesse mesmo script, o `kill -9` só matava o subshell, e o `cat` filho seguia
  segurando o socket — o servidor nunca via a desconexão e o teste não provava
  nada. Foi a asserção do `353` que pegou isso.

Itens do `TASKS.md`: `cmdJoin — canal novo` e `cmdJoin — sequência`.

---

## 7. Passo 2 — `cmdJoin`: múltiplos canais e os portões de modo

### Implementar

- `utils::split(params[0], ',')` para os canais e `utils::split(params[1], ',')`
  para as chaves, **posicionalmente**: a chave *i* pertence ao canal *i*, e
  campo vazio significa "sem chave" — é exatamente por isso que o `split`
  preserva campo vazio. `JOIN #a,#b ,key2` dá chave só ao `#b`;
- cada canal é processado de forma independente: um `403` num não impede a
  entrada nos outros;
- portões, **antes** do `addMember` e nesta ordem:
  - `+k` e chave errada ou ausente → `475 ERR_BADCHANNELKEY`;
  - `+i` e não convidado → `473 ERR_INVITEONLYCHAN`;
  - `+l` e `memberCount() >= limite` → `471 ERR_CHANNELISFULL`;
- entrada por convite **consome o convite**: `removeInvite` no sucesso;
- um canal criado agora nunca tem modo, então o caminho de criação não muda.

### Testes de unidade

```
JOIN #a,#b            -> entra nos dois, duas sequências completas
JOIN #a,#b key1,key2  -> chave posicional; com key errada no #b, entra no #a e leva 475 no #b
JOIN #a,#b ,key2      -> #a sem chave, #b com key2   (o caso do campo vazio)
JOIN #bad,#ok         -> 403 no primeiro e entrada normal no segundo
canal +k, chave certa -> entra;  chave errada -> 475;  sem chave -> 475
canal +i, sem convite -> 473;  com addInvite  -> entra E o convite foi consumido
canal +l com 2 membros e limite 2 -> 471;  limite 3 -> entra
+i tem precedência sobre +l?  documentar a ordem escolhida e travá-la num teste
```

Os modos são ligados direto pela API do `Channel` (`setInviteOnly`, `setKey`,
`setUserLimit`) — `cmdMode` ainda não existe, e não precisa existir.

### Integração

Estende `tests/it/join.sh` com o caso multi-canal e um `475`.

### Pronto quando

`make test` verde e `/join #a,#b` no irssi abre **duas** janelas.

Itens do `TASKS.md`: `cmdJoin — múltiplos canais` e `cmdJoin — +i/+k/+l`.

---

## 8. Passo 3 — `cmdPart`

### Implementar

- `461` sem parâmetro; `403` se o canal não existe ou o nome é inválido;
- `442 ERR_NOTONCHANNEL` se não é membro;
- lista de canais separada por vírgula, como no `JOIN`;
- **broadcast primeiro, remoção depois**: `:nick!user@host PART #chan :razão`
  vai para todos os membros **incluindo quem sai** (o irssi usa esse eco para
  fechar a janela), e só então `removeMember` + `removeOperator`;
- canal vazio depois disso → `server.removeChannel(nome)`. Depois dessa
  chamada o ponteiro `Channel*` está **morto**: não toque nele.

### Testes de unidade

```
PART sem parâmetro          -> 461
PART #nope                  -> 403
PART de quem não é membro   -> 442
PART com razão              -> todos os membros, o que sai incluído, recebem o PART
PART sem razão              -> formato sem trailing (ou razão default — trave a escolha)
último membro sai           -> findChannel devolve NULL (canal removido)
operador sai, sobra um membro -> o canal continua, e o que sobrou NÃO ganha op
```

O último caso merece pergunta explícita antes de virar teste: **um canal que
perde o último operador continua sem operador nenhum.** É o comportamento dos
servidores reais e é o mais simples de defender.

### Integração — `tests/it/part.sh` (novo)

Dois clientes, um sai, o outro tem que ver a linha `PART`. Depois os dois saem
e um terceiro entra: ele tem que virar **operador**, provando que o canal foi
mesmo destruído e recriado.

### Pronto quando

Dois irssi: `/part` fecha a janela de quem saiu e imprime a saída na janela de
quem ficou.

---

## 9. Passo 4 — `cmdPrivmsg` para canal

### Implementar

- sem alvo → `411 ERR_NORECIPIENT` (`:No recipient given (PRIVMSG)`);
- sem texto → `412 ERR_NOTEXTTOSEND`;
- alvo começando com `#`: `403` se não existe; `404` se o remetente não é
  membro (**D13**);
- `broadcastToChannel(canal, linha, &sender)` — o `except` é o remetente, que
  **não** recebe o próprio `PRIVMSG` de volta. Essa assimetria em relação ao
  `JOIN` é do protocolo, não nossa: o cliente já mostrou a própria mensagem.

### Testes de unidade

```
PRIVMSG                       -> 411
PRIVMSG #room                 -> 412
PRIVMSG #room :               -> 412?  (trailing vazio — decidir e travar)
PRIVMSG #nope :oi             -> 403
não-membro fala no canal      -> 404
membro fala                   -> os OUTROS recebem ":nick!user@host PRIVMSG #room :oi"
                              -> o remetente NÃO recebe nada
texto com espaços e ':' no meio chega inteiro (prova o trailing do parser)
```

### Integração — `tests/it/privmsg.sh` (novo)

Inclui o caso do `TASKS.md` da Fase 4 que só agora fica exercitável de verdade:
**`PRIVMSG` de 504 bytes**. Legal na entrada (< 512); com
`:nick!user@host PRIVMSG #room :` na frente, a linha de saída passa de 512 e
tem que chegar ao destinatário **truncada em 510 + CRLF**. O mecanismo já está
testado em `test_server.cpp`; o que falta é o caso com um `PRIVMSG` real.

### Pronto quando

Dois irssi na mesma janela de canal conversam.

---

## 10. Passo 5 — `cmdPrivmsg` para usuário

### Implementar

Alvo que não começa com `#`: `findClientByNick`; `NULL` → `401 ERR_NOSUCHNICK`.
Achou → `sendToClient` com a mesma linha `fromClient`.

### Testes

Unidade cobre `401` (em unidade `findClientByNick` sempre devolve `NULL` —
§1.1 item 3). O **caminho feliz é do script**: `tests/it/privmsg.sh` ganha duas
conexões registradas e um `PRIVMSG bob :oi`, com asserção nos bytes que chegam
ao bob.

### Pronto quando

`/msg bob oi` num irssi abre a janela de query no outro.

Itens do `TASKS.md`: as duas linhas de `cmdPrivmsg`.

---

## 11. Passo 6 — `cmdTopic`

### Implementar

- `461` sem parâmetro; `403` canal inexistente; `442` se não é membro;
- **consulta** (só o nome do canal, `hasTrailing == false`): `332` com o tópico,
  ou `331` se está vazio. Consultar **não** exige ser operador;
- **alteração** (veio trailing, mesmo vazio): se `isTopicRestricted()` e o
  remetente não é operador → `482 ERR_CHANOPRIVSNEEDED`. Senão `setTopic` e
  broadcast `:nick!user@host TOPIC #chan :novo` para todos os membros;
- `TOPIC #c :` (trailing vazio) **limpa** o tópico — é para isso que
  `hasTrailing` existe no parser.

### Testes de unidade

```
TOPIC sem parâmetro / canal inexistente / não-membro  -> 461 / 403 / 442
consulta sem tópico   -> 331
consulta com tópico   -> 332 :<texto>
alteração sem +t por não-operador  -> funciona (e faz broadcast)
alteração com +t por não-operador  -> 482, e o tópico NÃO muda
alteração com +t por operador      -> funciona
TOPIC #c :   (vazio)               -> limpa, e a consulta seguinte dá 331
```

### Pronto quando

No irssi: `/topic` mostra, `/topic novo assunto` muda e aparece na barra dos
dois clientes; com `+t`, um não-operador leva "You're not channel operator".

---

## 12. Passo 7 — `cmdKick`

### Implementar

`KICK <canal> <nick> [:razão]`

- `461` com menos de dois parâmetros; `403` canal inexistente;
- `442` se **o remetente** não é membro;
- `482` se o remetente não é operador;
- alvo resolvido **varrendo `getMembers()` por nick** (D10); não achou → `441`;
- broadcast `:nick!user@host KICK #chan vitima :razão` a **todos os membros,
  a vítima incluída** — é assim que o cliente dela sabe fechar a janela — e só
  então `removeMember`/`removeOperator` da vítima;
- canal vazio depois disso → `removeChannel`;
- razão default quando não veio trailing: o nick de quem chutou (convenção
  comum) — decidir e travar num teste.

### Testes de unidade

Todos, inclusive o caminho feliz, porque D10 tirou a dependência de
`Server::_clients`.

```
KICK #c            -> 461
KICK #nope alice   -> 403
remetente não-membro  -> 442
membro não-operador   -> 482
operador chuta alguém que não está no canal -> 441
operador chuta membro -> todos recebem o KICK, a vítima incluída
                      -> a vítima deixa de ser membro
                      -> a vítima continua sendo um Client válido (nada foi deletado)
operador chuta o último outro membro e sai  -> canal removido
alvo por nick com CASE diferente -> funciona (equalsIgnoreCase)
```

### Pronto quando

`/kick #test bob` fecha a janela do bob e imprime a linha no canal.

---

## 13. Passo 8 — `cmdInvite`

### Implementar

`INVITE <nick> <canal>` (nesta ordem — é o que o RFC manda no comando)

- `461` com menos de dois parâmetros;
- alvo por `findClientByNick` → `401` se não existe;
- `442` se o remetente não é membro do canal;
- `482` se o canal é `+i` e o remetente não é operador. (Em canal sem `+i`,
  qualquer membro pode convidar — comportamento padrão);
- `443 ERR_USERONCHANNEL` se o alvo já é membro;
- `channel->addInvite(alvo)`, `341` para quem convidou **na ordem
  `<nick> <channel>` (D8)**, e `:quem!user@host INVITE alvo :#canal` para o
  convidado;
- o convite guarda `Client*`, não nick — sobrevive a `/nick` de graça, e o
  `sweepChannels` já limpa na desconexão.

### Testes

Unidade cobre `461` e `401`. O resto — que precisa de um alvo achável — vai
para `tests/it/invite.sh`: convida, o convidado entra num canal `+i`, e uma
segunda tentativa de entrar (convite consumido no Passo 2) dá `473`.

### Confirmar D8 aqui

Este é o passo que olha o `341` no irssi. Se ele mostrar o convite de forma
estranha, inverte a ordem e **anota no `ARCHITECTURE.md` §6** — não improvisa.

---

## 14. Passo 9 — `cmdMode`: parser, consulta e flag desconhecida

O `MODE` é o comando mais escorregadio da fase, então ele vem em **quatro**
passos, e este primeiro **não muda modo nenhum**.

### Implementar

O **Passo 0.6 já entregou** o esqueleto: `461`, o silêncio no alvo não-canal,
`403` e a consulta `324`. Este passo acrescenta o que falta para *alterar*
modos — e continua sem aplicar nenhum deles.

- **parser da string de modos**, sem aplicar nada ainda: percorre
  `+it-k` da esquerda para a direita, um sinal corrente que começa em `+`, e
  consome um parâmetro da lista **só** nas flags que pedem um:
  `+k`, `+l`, `+o`, `-o` pedem; `-k`, `-l`, `i`, `t` não pedem (`ARCHITECTURE.md`
  §8);
- flag fora de `itkol` → `472 ERR_UNKNOWNMODE` e **segue processando as
  outras**;
- `482` se o remetente não é operador — antes de aplicar qualquer coisa.

### Testes de unidade

```
não-operador tentando ALTERAR              -> 482
MODE #c +z                                 -> 472 z :is unknown mode char to me
MODE #c +zi                                -> 472 no z E o i aplicado
```

Vale um teste dedicado só ao consumo posicional de parâmetros, que é onde essa
função costuma errar:

```
MODE #c +kl segredo 10   -> chave "segredo", limite 10
MODE #c -k+l 10          -> tira a chave (sem parâmetro) e põe limite 10
MODE #c +ko segredo bob  -> chave "segredo", op para o bob
```

---

## 15. Passo 10 — `cmdMode`: `i` e `t`

Os dois booleanos, sem parâmetro. Aplicar `setInviteOnly` / `setTopicRestricted`
e transmitir `:nick!user@host MODE #chan +i` a todos os membros.

### Testes

```
+i liga, -i desliga, e o broadcast sai nos dois casos
+t liga, -t desliga
+it numa tacada só  -> um único broadcast com "+it", os dois estados ligados
ligar o que já está ligado -> decidir: transmite igual, ou fica em silêncio? travar
```

Regressão que fecha o ciclo com o Passo 2: com `+i` ligado **por `MODE`**,
um `JOIN` sem convite dá `473`. Até aqui isso só tinha sido testado com o modo
ligado à mão pela API do `Channel`.

---

## 16. Passo 11 — `cmdMode`: `k` e `l`

### Implementar

- `+k <chave>`: `461` se falta o parâmetro; senão `setKey`;
- `-k`: `clearKey()`, **sem** exigir parâmetro;
- `+l <n>`: `utils::parseInt`; falhou ou `<= 0` → **ignora em silêncio** (D12);
  senão `setUserLimit`;
- `-l`: `clearUserLimit()`.

### Testes

```
+k segredo   -> hasKey, e o JOIN sem chave passa a dar 475
-k           -> !hasKey, e o JOIN sem chave volta a funcionar
+k sem parâmetro -> 461, e a chave NÃO muda
+l 3         -> limite 3;  o quarto JOIN dá 471
+l 0 / +l abc / +l -5 -> ignorado em silêncio, sem numeric, limite intacto (D12)
-l           -> limite some, o JOIN volta a passar
```

---

## 17. Passo 12 — `cmdMode`: `o`

- alvo resolvido **varrendo os membros** (D10) → `441` se não está no canal;
- `461` se `+o`/`-o` vieram sem nick;
- `addOperator` / `removeOperator` e broadcast
  `:nick!user@host MODE #chan +o bob`;
- um operador **pode** se rebaixar (`-o` em si mesmo). O canal pode acabar sem
  operador nenhum — mesmo desfecho do Passo 3, e o mesmo argumento.

### Testes

```
+o em membro     -> isOperator true, broadcast sai
+o em quem não é membro -> 441
-o em operador   -> perde o op, e um MODE seguinte dele dá 482
+o sem nick      -> 461
não-operador tentando +o -> 482
o novo operador aparece com "@" num 353 posterior
```

---

## 18. Passo 13 — Convergência final e fechamento da fase

### Fazer

1. **Dois irssi de verdade**, o roteiro do `PLANO.md` §3 ponta a ponta:
   conectar, `/join`, conversar, `/msg` privado, `/topic`, `/mode +t`, `/op`,
   `/kick`, `/invite` num canal `+i`, `/part`, `/quit`.
2. **Suíte inteira**: `make test` e todos os `tests/it/*.sh` num laço.
3. **valgrind** com os comandos de canal em uso — o item do `TASKS.md` que diz
   "refazer com os comandos de canal":
   `valgrind --leak-check=full ./ircserv 6667 secret`, dois clientes, canal
   criado, `KICK`, `INVITE`, `QUIT` sem sair do canal, `Ctrl+C` no fim.
   **0 bytes definitely lost, 0 erros.**
4. **`QUIT` colado com outra linha, agora com canais** — o caso de
   use-after-free do `session.sh`, repetido com o cliente dentro de um canal.
5. **Documentos**: `ARCHITECTURE.md` §6 com D8 e D9 escritos na tabela (e a
   nota "escolha uma e verifique" apagada); `TASKS.md` com os 17 itens da Fase 3
   e as linhas da Fase 4 que este trabalho fechou; `README.md` §"integration
   tests" listando os scripts novos e removendo o aviso de que o
   `channel_seam.sh` se pula sozinho.

### Pronto quando

Os 17 itens da Fase 3 estão `done` no `TASKS.md`, o valgrind está limpo com
canais em uso, e a `main` recebe a branch.

---

## 19. Arquivos que esta fase cria ou toca

| Arquivo | O quê |
|---|---|
| `src/CommandsRegistration.cpp` | Passo 0.5 — `cmdCap` passa a responder (D17) |
| `tests/test_cmd_session.cpp` | Passo 0.5 — `CAP` ganha teste próprio |
| `tests/it/session.sh` | Passo 0.5 — a asserção do `CAP` inverte de sentido |
| `docs/FASE2.md` | Passo 0.5 — D2 marcada como superada pela D17 |
| `src/CommandsChannel.cpp` | **novo no Passo 0.6** (`cmdMode` mínimo), depois os outros seis handlers (D11) |
| `src/CommandTable.cpp` | descomentar uma entrada por passo |
| `tests/test_cmd_channel.cpp` | **novo no Passo 0.6** — testes de unidade dos handlers |
| `tests/harness.hpp` / `tests/test_main.cpp` | uma declaração e uma chamada |
| `tests/test_server.cpp` | `testCommandTable` acompanha a tabela crescendo |
| `tests/it/channel_seam.sh` | registro completo antes do `JOIN`; para de se pular |
| `tests/it/join.sh`, `part.sh`, `privmsg.sh`, `invite.sh`, `mode.sh` | **novos** |
| `docs/ARCHITECTURE.md` | §6: D8 e D9 na tabela de numerics |
| `docs/TASKS.md` | status dos itens, e o dono da Fase 3 |
| `docs/README.md` | lista dos scripts de integração |
| `docs/FASE3.md` | este arquivo: status e D15 |

Nenhum header do contrato muda. Nenhuma entrada nova no seam do `Server` —
o que é, por si só, a evidência de que a Fase 2 acertou o desenho.

---

## 20. Bloqueios desta fase

| O quê | Quem | Desde | Estado |
|---|---|---|---|
| irssi não instalado | Eduardo | 2026-08-24 | **resolvido** — `irssi 1.4.5` instalado |
| D15 (`WHO`/`WHOIS`) dependia da captura do Passo 0 | Eduardo | 2026-08-24 | **resolvido** — nem um nem outro; ver D15 |
| **O irssi não completa o registro: trava no `CAP LS` (D17)** | Eduardo (Passo 0.5) | 2026-08-24 | **aberto — bloqueia toda a Fase 3** |

Também vale anotar, porque o `TASKS.md` diz o contrário: o item 1 do
`PLANO.md` §3 Fase 3, "registrar no irssi", **nunca foi verificado no irssi** —
a Fase 2 o deu por pronto com base em `nc` e nos scripts. Ele só fecha no
Passo 0.5.
