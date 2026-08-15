# FASE 2 — Servidor vivo (plano de execução)

Plano passo a passo da **trilha TRANSPORT** (Eduardo) para a Fase 2 descrita em
[PLANO.md](PLANO.md). O contrato técnico continua sendo
[ARCHITECTURE.md](ARCHITECTURE.md); a lista de itens continua sendo
[TASKS.md](TASKS.md). **Este arquivo é o rastreador do dia a dia da Fase 2** —
atualize o status no mesmo commit que muda o código.

O laço de `poll()` está marcado como sessão conjunta no `PLANO.md`, mas será
feito só pelo Eduardo. Por isso ele é construído em **três fatias** (esqueleto →
leitura → escrita) em vez de uma sentada só: cada fatia é pequena o bastante
para ser defendida linha a linha na avaliação.

**Status:** `todo` → `doing` → `done` (funciona e tem teste) → `integrated`
(mergeado na `main` via PR).

---

## 1. Visão geral

| # | Passo | Entrega observável | Status |
|---|---|---|---|
| 0 | Decisões e reserva de nomes | `TASKS.md` atualizado, colega avisado | todo |
| 1 | Socket de escuta, sem `poll()` | `ss -ltn` mostra a porta; SIGINT sai limpo | todo |
| 2 | Esqueleto do `poll()` + `accept` + reap | 3 clientes simultâneos, 0% de CPU ocioso | todo |
| 3 | Caminho de leitura (`recv` → buffer → linhas) | **teste do subject (`com^Dman^Dd`) passa** | todo |
| 4 | Caminho de escrita (`sendToClient` + `POLLOUT`) | truncagem em 510 e SendQ no `make test` | todo |
| 5 | Despachante (parse, tabela, `421`, `451`) | `FOO` → 421, `JOIN` sem registro → 451 | todo |
| 6 | `PASS`/`NICK`/`USER` + rajada `001`–`004` | registro ponta a ponta pelo `nc` | todo |
| 7 | `PING`/`PONG`, `QUIT`, `CAP` | `QUIT` colado com outra linha não quebra | todo |
| 8 | Endurecimento e fechamento da fase | valgrind limpo, 50 clientes, `TASKS.md` | todo |
| 9 | Gancho de integração — **depende de `Channel.cpp`** | seam de canal real, tabela completa | todo (bloqueado) |

Os passos 1–8 **não dependem de nada da trilha DOMAIN**. O passo 9 é o único
que espera o colega. Ver seção 3.

---

## 2. Decisões desta fase

Anotadas aqui para não serem redecididas, e para o colega ver sem perguntar.

**Todas aceitas em 2026-08-15** como padrão de trabalho, para a fase não ficar
parada esperando o colega. Só o **D2** mexe num arquivo que a outra trilha
também edita (`Command.hpp`); os outros ficam inteiros dentro de código do
TRANSPORT, então um conflito, se vier, custa uma linha para reverter.

| # | Decisão | Valor | Status |
|---|---|---|---|
| D1 | Nome do servidor em `:<servername>` | string fixa `"ircserv"` no código. **Não** derivar do hostname da máquina: isso exigiria `gethostname`, que não está na lista de funções externas permitidas do subject. (Cuidado: a lista tem `gethostbyname`, que é outra coisa — resolve nome → endereço IP.) | aceita |
| D2 | `CAP` | handler no-op (ignora em silêncio) em vez de `421`: o irssi imprime o 421 na janela de status, e o subject exige conectar "without encountering any error". **Custo: declarar `cmdCap` no `Command.hpp` (header compartilhado)** | aceita |
| D3 | `004 RPL_MYINFO` | `ircserv 1.0 - itkol` — nenhum modo de usuário, os cinco modos de canal que implementamos | aceita |
| D4 | `003 RPL_CREATED` | `__DATE__ " " __TIME__` — sem syscall, fácil de explicar | aceita |
| D5 | Chave do mapa `_channels` | `utils::toIrcLower(nome)`; `Channel::getName()` guarda a grafia original para exibição. **Contrato entre as trilhas — anotar no `ARCHITECTURE.md` §5** | aceita |
| D6 | Testes de integração versionados | sim, em `tests/it/*.sh`, fora do build avaliado | aceita |

---

## 3. Como a Fase 2 anda sem a trilha DOMAIN

### 3.1 O problema é de link, não de projeto

`include/Channel.hpp` existe, mas `src/Channel.cpp` não. O `Makefile` faz
`wildcard src/*.cpp` e linka todo objeto nos **dois** binários. Então qualquer
corpo de função que chame um método de `Channel`, construa um, ou dê `delete`
num, gera `undefined reference` e **para o build** — inclusive o `make test`.

O membro `std::map<std::string, Channel*> _channels` em si está **ok**: guarda
ponteiros, e tipo incompleto por ponteiro não custa nada. Só *corpos* quebram.

Cinco lugares do `Server` querem `Channel`:

| # | Onde | Precisa de |
|---|---|---|
| 1 | `findChannel` / `getOrCreateChannel` / `removeChannel` | `Channel(name)`, `~Channel`, `getName()` |
| 2 | `broadcastToChannel` | `getMembers()` |
| 3 | `broadcastToPeers` | `getMembers()`, `isMember()` |
| 4 | `reapDisconnected` → varrer membros/operadores/convites | `removeMember` / `removeOperator` / `removeInvite` |
| 5 | `~Server` → `delete` de cada canal | `~Channel` |

### 3.2 O mecanismo: um arquivo descartável

Todo corpo que toca em `Channel` fica em **um arquivo que depois é apagado**,
em vez de `// TODO` espalhado por três funções:

- **`src/ServerChannels.stub.cpp`** — Fase 2. Define esses símbolos como no-op
  (`findChannel` → `NULL`, broadcasts → nada, varredura → nada). Compila, linka,
  e é **correto** na Fase 2 porque `_channels` está comprovadamente sempre
  vazio: nada cria canal quando `getOrCreateChannel` devolve `NULL` e nenhum
  handler de `JOIN` está registrado.
- **`src/ServerChannels.cpp`** — o de verdade, escrito no dia em que
  `Channel.cpp` existir. O stub sai com `git rm` no mesmo commit.

Dois helpers privados entram na seção `private` do `Server.hpp` (livre para
mudar, `ARCHITECTURE.md` §3): `sweepChannels(Client &)` e `clearAllChannels()`.
`reapDisconnected` e `~Server` chamam esses helpers e nunca mencionam `Channel`.

Na integração isso vira **um arquivo apagado e um arquivo criado** — sem
conflito de merge, sem TODO esquecido, e o próprio stub documenta o que falta.

### 3.3 Problemas de compatibilidade previstos

Em ordem de quanto doeriam:

1. **`buildCommandTable` é uma função só que duas pessoas precisam preencher.**
   O `TASKS.md` dá ela ao TRANSPORT, mas se a sessão do colega escrever a dela
   em `src/Command.cpp`, dá símbolo duplicado e **nenhum** binário linka. E se
   eu registrar `cmdJoin`…`cmdMode` antes dos corpos existirem, quebra o **meu**
   build. Solução: eu escrevo, em `src/CommandTable.cpp`, só as entradas de
   transport, uma por linha, com um bloco marcado onde entram as 7 do colega.
2. **A grafia da chave do mapa de canais é contrato não escrito** (D5).
   `#Chan` e `#chan` são o mesmo canal. Se o colega assumir outra coisa,
   `JOIN #Dev` seguido de `PRIVMSG #dev` cria dois canais em silêncio.
3. **Colisão de nome de arquivo.** Duas sessões cegas, os mesmos nomes óbvios.
   Reservados para TRANSPORT: `Server.cpp`, `ServerChannels.cpp`,
   `CommandTable.cpp`, `CommandsRegistration.cpp`. Sugeridos para DOMAIN:
   `Channel.cpp`, `CommandsChannel.cpp`.
4. **`tests/harness.hpp` e `tests/test_main.cpp` são compartilhados** e os dois
   vão editar: cada arquivo de teste novo adiciona uma declaração e uma chamada.
   Conflito pequeno e garantido; resolve-se mantendo as duas linhas.
5. **A desconexão adiada é estrutural para o colega também**, não só para mim.
   Os handlers dele iteram `channel.getMembers()` enquanto `broadcastToChannel`
   pode marcar um cliente. Isso só funciona porque nada é deletado dentro da
   iteração. Se ele pedir uma desconexão "imediata", a resposta é não.
6. **Nunca copiar um `Client` depois que ele está em `_clients`.** `Client` é
   copiável (a OCF exige), mas `Channel` guarda `Client*` e a lista de convites
   depende de identidade de ponteiro. Uma cópia acidental e o convite aponta
   para um cadáver. Passar sempre `Client*` / `Client&`.
7. **`getMembers()` devolve `std::set<Client*>`** — a ordem de iteração é a dos
   endereços, não determinística entre execuções. Nenhum teste dos dois lados
   pode depender da ordem dos nicks no `353`.

### 3.4 O que pedir ao colega agora

O `Channel` da Fase 1 dele (as 4 primeiras linhas do `TASKS.md`: OCF, membros,
operadores, convites) é o **caminho crítico da integração** — não os handlers.
Os handlers dele não linkam enquanto não existirem `Channel.cpp` **e** o meu
`ServerChannels.cpp` de verdade. Pedir que ele suba `Channel.cpp` cedo, mesmo
com os modos ainda vazios.

---

## 4. Passos

### Passo 0 — Decisões e reserva de nomes

**Status:** `todo`

Antes de qualquer código, porque é o que impede as duas sessões de divergirem.

- [x] Confirmar D1–D6 da seção 2 — aceitas em 2026-08-15
- [ ] Anotar D5 (grafia da chave de canal) no `ARCHITECTURE.md` §5
- [ ] Avisar o colega do D2 (`cmdCap` novo no `Command.hpp`) — é a única
      decisão que toca arquivo dele
- [ ] Registrar em `TASKS.md` (tabela de bloqueios): dono de `buildCommandTable`,
      nomes de arquivo reservados, e o pedido de `Channel.cpp` cedo
- [ ] Commit e push, para o colega ver sem precisar perguntar

---

### Passo 1 — Socket de escuta, sem `poll()` ainda

**Status:** `todo`

**Escrever:** `src/Server.cpp` (construtor, destrutor, `stop`,
`setupListenSocket`), `src/ServerChannels.stub.cpp`, os dois helpers privados no
`Server.hpp`, e religar o `main.cpp`.

O construtor põe `_listenFd = -1` e **não toca na rede** — é isso que permite
construir um `Server` na pilha nos testes unitários dos passos 4 e 6.
`setupListenSocket()` roda a partir do `run()`:
`socket` → `setsockopt(SO_REUSEADDR)` → `fcntl(fd, F_SETFL, O_NONBLOCK)` →
`bind` → `listen`.

`signal(SIGPIPE, SIG_IGN)` e `signal(SIGINT, ...)` ficam no `main`, antes de o
`Server` ser construído. O handler de SIGINT escreve num
`volatile sig_atomic_t g_shutdown` e mais nada.

**Testes:**

```sh
make && ./ircserv 6667 secret &
ss -ltn | grep 6667                     # LISTEN na 6667
./ircserv 6667 secret                   # segundo bind -> erro limpo, exit 1, sem crash
kill -INT %1                            # sai sozinho, não é morto
valgrind --leak-check=full ./ircserv 6667 secret   # Ctrl+C -> 0 vazamentos
make test                               # continua verde: o stub linka
```

- [ ] `ss -ltn` mostra a porta
- [ ] segundo bind falha limpo
- [ ] SIGINT sai com código 0
- [ ] valgrind sem vazamento
- [ ] `make test` verde

**Saber explicar:** por que `SO_REUSEADDR` (TIME_WAIT no endereço de escuta);
por que o socket de escuta também é não bloqueante (`accept` pode bloquear
mesmo depois do `poll` dizer legível); por que o handler só escreve um
`sig_atomic_t` (segurança assíncrona de sinal).

---

### Passo 2 — Esqueleto do `poll()` + `accept` + reap

**Status:** `todo`

**Escrever:** `run()`, `buildPollFds()`, `acceptNewClient()`,
`reapDisconnected()`. Ainda sem `recv`.

```cpp
while (_running && !g_shutdown)
{
    buildPollFds();                              // de _listenFd + _clients, toda iteração
    int ready = poll(&_pollFds[0], _pollFds.size(), -1);
    if (ready < 0)
    {
        if (errno == EINTR)
            continue;                            // SIGINT cai aqui; a flag tira do laço
        break;
    }
    for (size_t i = 0; i < _pollFds.size(); ++i)
    {
        short   re = _pollFds[i].revents;
        int     fd = _pollFds[i].fd;

        if (re == 0)
            continue;
        if (fd == _listenFd)
        {
            if (re & POLLIN)
                acceptNewClient();               // NÃO empurra em _pollFds
            continue;
        }
        if (re & POLLIN)
            handleReadable(fd);                  // ler ANTES de agir no HUP
        if (re & (POLLERR | POLLHUP | POLLNVAL))
            markDead(fd);
        else if (re & POLLOUT)
            handleWritable(fd);
    }
    reapDisconnected();                          // único lugar onde um Client é deletado
}
```

Três decisões para defender: `_pollFds` é **reconstruído** a cada iteração em
vez de mutado (imune a deslocamento de índice e invalidação de iterador; O(n)
não custa nada nesta escala); `acceptNewClient` nunca empurra em `_pollFds` no
meio do laço (o cliente novo ganha entrada na iteração seguinte, um ciclo
depois, o que não custa nada); `POLLIN` é tratado **antes** de `POLLHUP`, porque
um peer meio fechado ainda pode ter dados não lidos no buffer do kernel.

`acceptNewClient`: `accept` → `fcntl(O_NONBLOCK)` → hostname via `inet_ntop`
sobre o endereço do peer (nada de DNS reverso) → `new Client(fd, host)` →
inserir em `_clients`.

**Testes:**

```sh
./ircserv 6667 secret &
for i in 1 2 3; do nc 127.0.0.1 6667 & done   # 3 ao mesmo tempo, sem travar
kill -9 %2                                     # servidor sobrevive, os outros seguem
ls /proc/$(pgrep ircserv)/fd | wc -l           # volta ao normal: sem vazar fd
top -p $(pgrep ircserv)                        # ocioso com clientes: ~0.0% de CPU
```

- [ ] 3 clientes simultâneos sem travar
- [ ] `kill -9` num cliente não derruba nem afeta os outros
- [ ] contagem de fds volta depois das desconexões
- [ ] CPU ociosa em ~0%

A checagem de CPU não é estética: é a primeira coisa que o avaliador olha, e é
o que o `poll(..., -1)` mais o `POLLOUT` ainda não armado compram.

---

### Passo 3 — Caminho de leitura: `recv` → `appendToReadBuffer` → drenar linhas

**Status:** `todo`

**Escrever:** `handleReadable(fd)`.

- Um `recv` de `irc::RECV_CHUNK` por evento de prontidão — nunca laço até
  `EAGAIN` (`ARCHITECTURE.md` §11 explica o porquê).
- `n == 0` → o peer fechou → desconecta.
- `n < 0` com `EAGAIN`/`EWOULDBLOCK` → volta; não é erro nem desconexão.
- `EINTR` → volta; o próximo `poll` avisa de novo.
- `std::string(buf, n)` — nunca a forma de um argumento só.
- `appendToReadBuffer` retornando `false` → `ERROR :Request too long` +
  `disconnectClient`.
- Drenar com `while (!client.isDisconnecting() && client.extractCommand(line))`.

`handleLine` é um **eco temporário só neste passo**, para a drenagem ficar
observável antes de o despachante existir.

**Testes — é aqui que o teste avaliado passa.** Versão roteirizada, sem depender
das variações de `nc`:

```sh
exec 3<>/dev/tcp/127.0.0.1/6667
printf 'com' >&3; sleep 0.3; printf 'man' >&3; sleep 0.3; printf 'd\r\n' >&3
timeout 2 cat <&3          # exatamente uma linha: "command"
```

E, na mão, o teste literal do subject (`nc -C 127.0.0.1 6667`, `com^Dman^Dd`) —
é o que o avaliador digita.

- [ ] pacote parcial roteirizado vira um comando só
- [ ] `nc -C` com Ctrl+D na mão vira um comando só
- [ ] dois comandos num `printf` só viram dois comandos
- [ ] 600 bytes + CRLF viram uma linha de 510
- [ ] 5000 bytes sem `\n` → `ERROR :Request too long` e fecha
- [ ] `\n` sozinho (sem `\r`) funciona igual

---

### Passo 4 — Caminho de escrita: `sendToClient` + `POLLOUT`

**Status:** `todo`

**Escrever:** `sendToClient` (truncar em `irc::MAX_PAYLOAD_LEN`, *depois*
acrescentar `\r\n`, depois `queueOutput`; `false` → `disconnectClient("SendQ
exceeded")`), `handleWritable` (um `send`, `consumeOutput(n)`, `EAGAIN` não é
fatal), e o `POLLOUT` armado no `buildPollFds` **só** quando
`hasPendingOutput()`.

**É aqui que o `make test` começa a cobrir a Fase 2.** Arquivo novo
`tests/test_server.cpp` + `runServerTests()` no `harness.hpp` e no
`test_main.cpp`. Um `Server` na pilha não abre socket nenhum e `sendToClient`
não toca em fd, então:

```cpp
Server  s(6667, "secret");
Client  c(-1, "localhost");

s.sendToClient(c, std::string(600, 'a'));
check(c.getOutputBuffer().size() == irc::MAX_PAYLOAD_LEN + 2,
      "linha de saida truncada em 510 + CRLF");

for (int i = 0; i < 200; ++i)               // 200 * 512 > MAX_OUTPUT_QUEUE
    s.sendToClient(c, std::string(510, 'x'));
check(c.isDisconnecting(),
      "estouro de SendQ marca o cliente, nao deleta");
```

- [ ] truncagem em 510 no `make test`
- [ ] estouro de SendQ marca (e não deleta) no `make test`
- [ ] eco do passo 3 agora sai por `sendToClient`, com CRLF correto
- [ ] CPU ociosa ainda em ~0% com cliente conectado e fila vazia

---

### Passo 5 — Despachante: parse, tabela, `421`, `451`

**Status:** `todo`

**Escrever:** o `handleLine` de verdade e `src/CommandTable.cpp`.

- `parseMessage` → comando vazio significa **ignorar em silêncio**, nunca
  responder.
- Comando em maiúsculas antes da busca (comandos de IRC são
  case-insensitive).
- Busca no mapa; não achou → `421`.
- Porta do registro **no despachante, nunca num handler**: se
  `!sender.isRegistered()` e o comando não é `PASS`/`NICK`/`USER`/`QUIT`/
  `PING`/`CAP` → `451` e para.

A tabela leva só as entradas de transport, mais o bloco marcado onde entram as
7 do DOMAIN. Neste passo os handlers de transport ainda são corpos vazios, para
a porta do registro ser testável sozinha.

**Testes:**

```sh
printf 'FOO\r\n'            | nc -C -q1 127.0.0.1 6667   # 421 FOO :Unknown command
printf 'JOIN #x\r\n'        | nc -C -q1 127.0.0.1 6667   # 451, não 421 nem crash
printf 'jOiN #x\r\n'        | nc -C -q1 127.0.0.1 6667   # também 451 -> maiusculiza
printf '\r\n\r\nPING x\r\n' | nc -C -q1 127.0.0.1 6667   # linha vazia não responde nada
```

- [ ] comando desconhecido → `421`
- [ ] comando de canal antes do registro → `451`
- [ ] mesmo comando em minúsculas → mesmo resultado
- [ ] linha vazia não gera resposta

---

### Passo 6 — `cmdPass` / `cmdNick` / `cmdUser` + rajada `001`–`004`

**Status:** `todo`

**Escrever:** `src/CommandsRegistration.cpp`. `461` sem parâmetros, `462`
depois de registrado, `464` + desconexão com senha errada, `431`/`432`/`433` no
`NICK`, e a rajada disparada **exatamente uma vez** na transição, controlada por
`welcomeSent()`.

**O arquivo de teste unitário mais rico da fase** (`tests/test_cmd_registration.cpp`),
tudo com `Server` e `Client` na pilha + `parseMessage`, verificando
`getOutputBuffer()`:

- [ ] senha errada → `464` **e** `isDisconnecting()`
- [ ] `NICK` antes do `PASS` → `451` (vindo da porta do despachante)
- [ ] alvo do numérico é `*` antes do registro e o nick depois
- [ ] `USER` com 3 parâmetros → `461`
- [ ] a rajada aparece uma única vez, na ordem `001 002 003 004`
- [ ] segundo `PASS` depois de registrado → `462`
- [ ] nick inválido (`~foo`, começando com dígito, > 30) → `432`

`433` é o único caso que um teste unitário não alcança de forma honesta: um nick
duplicado exige dois clientes dentro de `Server::_clients`, e clientes só entram
lá pelo `accept`. **Não abrir uma porta dos fundos no seam por causa disso** —
testar com duas conexões vivas, que é prova melhor de qualquer jeito:

```sh
printf 'PASS secret\r\nNICK alice\r\nUSER a 0 * :A\r\n' | nc -C -q5 127.0.0.1 6667 &
sleep 1
printf 'PASS secret\r\nNICK ALICE\r\nUSER b 0 * :B\r\n' | nc -C -q1 127.0.0.1 6667
# 433, comparação case-insensitive

printf 'PASS wrong\r\nNICK bob\r\n' | nc -C -q1 127.0.0.1 6667
# 464 e fecha

printf 'PASS secret\r\nNICK alice\r\nUSER alice 0 * :Alice\r\n' | nc -C -q1 127.0.0.1 6667
# 001, 002, 003, 004
```

- [ ] `433` com duas conexões vivas, insensível a maiúsculas
- [ ] `464` fecha a conexão
- [ ] registro completo devolve `001`–`004`

---

### Passo 7 — `PING`/`PONG`, `QUIT`, `CAP`

**Status:** `todo`

**Escrever:** `cmdPing` → `:<servername> PONG <servername> :<token>`;
`cmdPong` → aceita e ignora; `cmdQuit` → `disconnectClient`; `cmdCap` → no-op
(D2).

`disconnectClient` **retorna na hora se o cliente já está marcado**
(`ARCHITECTURE.md` §4): ele enfileira `ERROR :<reason>`, e num cliente com a
fila cheia esse enfileiramento falha, volta para `disconnectClient` e recursa
até acabar a pilha.

**Testes** — o caso de use-after-free é linha da Fase 4 no `TASKS.md`, mas
pertence a este passo, porque é este passo que o cria:

```sh
printf 'PASS secret\r\nNICK a\r\nUSER a 0 * :A\r\nQUIT :bye\r\nPRIVMSG #c :x\r\n' \
  | nc -C -q1 127.0.0.1 6667
# ERROR :..., fecha, servidor vivo, e o PRIVMSG nunca é despachado

printf 'CAP LS 302\r\n' | nc -C -q1 127.0.0.1 6667   # não derruba
```

- [ ] `PING token` devolve o mesmo token
- [ ] `QUIT` colado com outra linha: a segunda linha não é despachada
- [ ] o mesmo teste sob valgrind, sem uso de memória liberada
- [ ] `CAP LS 302` não derruba nem gera erro no cliente

---

### Passo 8 — Endurecimento e fechamento da fase

**Status:** `todo`

- [ ] bytes NUL e lixo binário (`head -c 200 /dev/urandom`) não derrubam
- [ ] 50 conexões simultâneas num laço
- [ ] `valgrind --leak-check=full` com clientes conectados no SIGINT: 0 vazamentos
- [ ] leitura de todo retorno de `recv`/`send`/`accept`/`poll`: nenhum ignorado
- [ ] `TASKS.md` atualizado (Fase 2 → `done`)
- [ ] decisões D1–D6 anotadas no `ARCHITECTURE.md`
- [ ] `README.md` com como rodar `tests/it/*.sh`

---

### Passo 9 — Gancho de integração (**bloqueado em `src/Channel.cpp`**)

**Status:** `todo` — bloqueado

- [ ] `git rm src/ServerChannels.stub.cpp`
- [ ] `src/ServerChannels.cpp` de verdade: `findChannel`, `getOrCreateChannel`,
      `removeChannel`, `broadcastToChannel`, `broadcastToPeers`,
      `sweepChannels`, `clearAllChannels`
- [ ] `disconnectClient` passa a transmitir o `QUIT` aos peers
- [ ] `cmdNick` passa a transmitir a troca de nick aos peers (`includeOrigin = true`)
- [ ] as 7 entradas de DOMAIN entram no `CommandTable.cpp`
- [ ] teste: cliente que sai de canal cheio não deixa ponteiro pendurado
      (valgrind + dois clientes num canal)

`broadcastToPeers` — o conjunto de destinatários deduplicado — é a peça
interessante e é minha. Dá para escrever e revisar durante o passo 8, desde que
fique na branch `feat/transport-channel-seam` para a `main` seguir verde.

---

## 5. Arquivos que esta fase cria ou toca

| Arquivo | Ação | Passo |
|---|---|---|
| `src/Server.cpp` | novo | 1–4 |
| `src/ServerChannels.stub.cpp` | novo, **descartável** | 1 (apagado no 9) |
| `src/ServerChannels.cpp` | novo | 9 |
| `src/CommandTable.cpp` | novo | 5 |
| `src/CommandsRegistration.cpp` | novo | 6–7 |
| `src/main.cpp` | edita | 1 |
| `include/Server.hpp` | edita — **só a seção `private`** | 1–2 |
| `include/Command.hpp` | edita — declarar `cmdCap` (D2) | 7 |
| `tests/test_server.cpp` | novo | 4 |
| `tests/test_cmd_registration.cpp` | novo | 6 |
| `tests/harness.hpp` / `tests/test_main.cpp` | edita — **compartilhado** | 4, 6 |
| `tests/it/*.sh` | novo | 3 em diante |
| `docs/TASKS.md` | edita | todo passo |

---

## 6. Bloqueios desta fase

Espelhar no `TASKS.md` para o colega ver.

| O quê | Quem destrava | Desde |
|---|---|---|
| `src/Channel.cpp` não existe → passo 9 parado, e os handlers do colega não linkam | DOMAIN | — |
| D1–D6 aceitas sem o colega ver; D2 e D5 ainda precisam ser comunicadas | Eduardo | 2026-08-15 |
