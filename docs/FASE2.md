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
| 0 | Decisões e reserva de nomes | `TASKS.md` atualizado, colega avisado | done |
| 1 | Socket de escuta, sem `poll()` | `ss -ltn` mostra a porta; SIGINT sai limpo | done |
| 2 | Esqueleto do `poll()` + `accept` + reap | 3 clientes simultâneos, 0% de CPU ocioso | done |
| 3 | Caminho de leitura (`recv` → buffer → linhas) | **teste do subject (`com^Dman^Dd`) passa** | done |
| 4 | Caminho de escrita (`sendToClient` + `POLLOUT`) | truncagem em 510 e SendQ no `make test` | done |
| 4.5 | Gancho de integração: seam de canal real | destrava os handlers do colega | done |
| 5 | Despachante (parse, tabela, `421`, `451`) | `FOO` → 421, `JOIN` sem registro → 451 | done |
| 6 | `PASS`/`NICK`/`USER` + rajada `001`–`004` | registro ponta a ponta pelo `nc` | done |
| 7 | `PING`/`PONG`, `QUIT`, `CAP` | `QUIT` colado com outra linha não quebra | done |
| 8 | Endurecimento e fechamento da fase | valgrind limpo, 50 clientes, `TASKS.md` | done |

Os passos 1–4 e 5–8 **não dependem de nada da trilha DOMAIN**. O 4.5 é o único
que precisa do `Channel.cpp`, e era o passo 9 até 2026-08-15 — foi promovido
quando o colega publicou o modelo pronto. Ver seção 3.

---

## 2. Decisões desta fase

Anotadas aqui para não serem redecididas, e para o colega ver sem perguntar.

**Todas aceitas em 2026-08-15**, e **confirmadas pela trilha DOMAIN no mesmo
dia** — ela também aceitou as reservas de nome de arquivo e a propriedade da
`buildCommandTable`, e informou que nada disso conflita com o código dela.
Só o **D2** mexe num arquivo que a outra trilha também edita (`Command.hpp`);
os outros ficam inteiros dentro de código do TRANSPORT.

| # | Decisão | Valor | Status |
|---|---|---|---|
| D1 | Nome do servidor em `:<servername>` | string fixa `"ircserv"` no código. **Não** derivar do hostname da máquina: isso exigiria `gethostname`, que não está na lista de funções externas permitidas do subject. (Cuidado: a lista tem `gethostbyname`, que é outra coisa — resolve nome → endereço IP.) | aceita |
| D2 | `CAP` | handler no-op (ignora em silêncio) em vez de `421`: o irssi imprime o 421 na janela de status, e o subject exige conectar "without encountering any error". **Custo: declarar `cmdCap` no `Command.hpp` (header compartilhado)** | ~~aceita~~ **SUPERADA pela D17 em 2026-08-24 — ver `FASE3.md`** |

> **Por que a D2 fica registrada em vez de ser reescrita.** O motivo dela estava
> certo (o `421` polui a janela de status do irssi); o remédio, não. Testada
> contra o irssi 1.4.5 pela primeira vez na Fase 3, a versão silenciosa deixou o
> cliente parado em `Waiting for CAP LS response...`: ele **nunca** manda
> `PASS`/`NICK`/`USER`, e ninguém consegue registrar. A D17 troca o silêncio por
> uma lista de capacidades vazia.
>
> O que essa decisão custou é o dado interessante: **toda a Fase 2 foi validada
> com `nc` e scripts, e nenhum deles podia pegar esse bug** — o `nc` não espera
> a resposta do `CAP` como um cliente de verdade espera. É o aviso do
> `ARCHITECTURE.md` §9 acontecendo com a gente.
| D3 | `004 RPL_MYINFO` | `ircserv 1.0 - itkol` — nenhum modo de usuário, os cinco modos de canal que implementamos | aceita |
| D4 | `003 RPL_CREATED` | `__DATE__ " " __TIME__` — sem syscall, fácil de explicar | aceita |
| D5 | Chave do mapa `_channels` | `utils::toIrcLower(nome)`; `Channel::getName()` guarda a grafia original para exibição. **Contrato entre as trilhas — anotar no `ARCHITECTURE.md` §5** | aceita |
| D6 | Testes de integração versionados | sim, em `tests/it/*.sh`, fora do build avaliado | aceita |
| D7 | Constness do `origin` em `broadcastToPeers` | Era `const Client &`, virou **`Client &`**. O `includeOrigin = true` exige *entregar* ao origin, e `sendToClient` pede referência não-const — o `const` descrevia a função de antes do `includeOrigin` existir, e custava um `const_cast` no corpo. Contraste proposital com o `except` do `broadcastToChannel`, que continua const porque é só comparado. **Não precisou de acordo prévio:** a mudança não pode divergir em silêncio (referência não-const liga sem problema; passar `const` seria erro de compilação), e os dois únicos comandos que chamam a função, `NICK` e `QUIT`, são do TRANSPORT | aceita 2026-08-18, anotada no `ARCHITECTURE.md` §4 |

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

### 3.4 O que pedir ao colega — RESOLVIDO em 2026-08-15

O pedido era: subir `src/Channel.cpp` cedo, mesmo com os modos ainda vazios,
porque ele é o caminho crítico da integração — não os handlers.

**Entregue, e completo.** `origin/domain`, commit `da0165f`: OCF inteira e as 29
funções do header, incluindo as sete de que o meu seam precisa (`getName`,
`getMembers`, `isMember`, `removeMember`, `removeOperator`, `removeInvite`,
`isEmpty`), mais 574 linhas de teste em `tests/test_channel.cpp`. Nenhuma
colisão de arquivo: ele não criou `Server.cpp`, `Command.cpp` nem
`CommandTable.cpp`.

Isso é o que permitiu promover o passo 9 para 4.5. **A dívida agora é minha:**
enquanto o `ServerChannels.cpp` de verdade não existir, os handlers dele linkam
contra o stub e dão segfault no primeiro `JOIN`, porque `getOrCreateChannel`
devolve `NULL` lá.

---

## 4. Passos

### Passo 0 — Decisões e reserva de nomes

**Status:** `done` — 2026-08-15

Antes de qualquer código, porque é o que impede as duas sessões de divergirem.

- [x] Confirmar D1–D6 da seção 2 — aceitas em 2026-08-15
- [x] Anotar D5 (grafia da chave de canal) no `ARCHITECTURE.md` §5, na
      subseção Casemapping
- [x] Avisar o colega do D2 (`cmdCap` novo no `Command.hpp`) — confirmado por
      DOMAIN em 2026-08-15
- [x] Registrar o dono de `buildCommandTable`, os nomes de arquivo reservados e
      o pedido de `Channel.cpp` cedo — tudo aceito por DOMAIN em 2026-08-15
- [x] Commit e push, para o colega ver sem precisar perguntar

---

### Passo 1 — Socket de escuta, sem `poll()` ainda

**Status:** `done` — 2026-08-15

**Escrever:** `src/Server.cpp` (construtor, destrutor, `stop`,
`installSignalHandlers`, `setupListenSocket`), `src/ServerChannels.stub.cpp`, os
dois helpers privados no `Server.hpp`, e religar o `main.cpp`.

O construtor põe `_listenFd = -1` e **não toca na rede** — é isso que permite
construir um `Server` na pilha nos testes unitários dos passos 4 e 6.
`setupListenSocket()` roda a partir do `run()`:
`socket` → `setsockopt(SO_REUSEADDR)` → `fcntl(fd, F_SETFL, O_NONBLOCK)` →
`bind` → `listen`.

`signal(SIGPIPE, SIG_IGN)` e `signal(SIGINT, ...)` ficam em
`Server::installSignalHandlers()`, chamada como **primeira linha do `run()`** —
antes de `setupListenSocket()`, portanto antes de qualquer fd existir, que é a
única ordem que importa. O plano original os punha no `main`; ficaram no
`Server` porque assim o `main` não precisa saber quais sinais o transporte usa,
e nada roda entre construir o `Server` e chamar `run()`. O handler de SIGINT
escreve num `volatile sig_atomic_t g_shutdown` e mais nada.

**Testes:**

```sh
make && ./ircserv 6667 secret &
ss -ltn | grep 6667                     # LISTEN na 6667
./ircserv 6667 secret                   # segundo bind -> erro limpo, exit 1, sem crash
kill -INT %1                            # sai sozinho, não é morto
valgrind --leak-check=full ./ircserv 6667 secret   # Ctrl+C -> 0 vazamentos
make test                               # continua verde: o stub linka
```

- [x] `ss -ltn` mostra a porta
- [x] segundo bind falha limpo (`Error: bind: Address already in use`, exit 1)
- [x] SIGINT sai com código 0, imprimindo `shutting down` (destrutor roda)
- [x] valgrind sem vazamento, nos dois caminhos: shutdown normal e `bind` falho
- [x] `make test` verde (173 passed) e `make && make` sem relink

**Saber explicar:** por que `SO_REUSEADDR` (TIME_WAIT no endereço de escuta);
por que o socket de escuta também é não bloqueante (`accept` pode bloquear
mesmo depois do `poll` dizer legível); por que o handler só escreve um
`sig_atomic_t` (segurança assíncrona de sinal).

---

### Passo 2 — Esqueleto do `poll()` + `accept` + reap

**Status:** `done` — 2026-08-18

**Escrever:** `run()`, `buildPollFds()`, `acceptNewClient()`,
`findClientByFd()`, `disconnectClient()`, `reapDisconnected()`, e o `recv` de
`handleReadable()` — ver a nota abaixo sobre por que ele não pôde esperar.

```cpp
while (_running && !g_shutdown)
{
    buildPollFds();                              // de _listenFd + _clients, toda iteração
    int ready = poll(&_pollFds[0], _pollFds.size(), -1);
    if (ready < 0)
    {
        if (errno == EINTR)
            continue;                            // SIGINT cai aqui; a flag tira do laço
        throw std::runtime_error(...);           // erro real: mensagem + exit 1.
                                                 // `break` sairia com codigo 0,
                                                 // fingindo shutdown limpo
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
        {
            Client  *client = findClientByFd(fd);

            if (client != NULL)
                disconnectClient(*client, "Connection closed by peer");
        }
        // POLLOUT entra aqui no passo 4, junto com o handleWritable
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

- [x] 3 clientes simultâneos sem travar (4 → 7 fds)
- [x] `kill -9` num cliente não derruba nem afeta os outros
- [x] contagem de fds volta depois das desconexões (7 → 6 → 4)
- [x] CPU ociosa em ~0% (0 ticks em 3 s com 3 clientes; 0 ticks em 4 s
      **depois** de um cliente mandar bytes)
- [x] valgrind com 2 clientes conectados no SIGINT: 0 vazamentos, 0 erros
      (exercita o `delete` dos clientes no `~Server`, que o passo 1 não tocava)

A checagem de CPU não é estética: é a primeira coisa que o avaliador olha, e é
o que o `poll(..., -1)` mais o `POLLOUT` ainda não armado compram.

**Por que o `recv` foi antecipado do passo 3 para cá.** O plano original dizia
"ainda sem `recv`", e isso não sobrevive à semântica do `poll()` no Linux: um
peer que fecha normalmente aparece como **`POLLIN` com `recv` devolvendo 0**,
não como `POLLHUP` — `POLLHUP` só é reportado quando as duas direções estão
fechadas. Com o `handleReadable` vazio, esse `POLLIN` nunca seria consumido, o
`poll()` voltaria na hora toda iteração (é *level-triggered*) e o laço giraria a
100% de CPU sem nunca reapear o cliente. Ou seja: dois checkboxes deste próprio
passo falhariam.

A divisão que ficou: **o passo 2 leva a syscall e a taxonomia de erro**
(`n == 0` → desconecta, `EAGAIN`/`EWOULDBLOCK`/`EINTR` → volta, outro erro →
desconecta) **e descarta os bytes**; o passo 3 só troca o descarte por
`appendToReadBuffer` + drenagem. Nada acima dessa linha muda.

**Também entrou aqui, porque o reap não linka sem ele:** o
`disconnectClient` mínimo — a guarda de reentrância (`isDisconnecting()` →
volta na hora) já está lá desde o primeiro dia, que é o que impede a recursão
infinita quando o passo 4 puser o `ERROR :<reason>` na fila de um cliente com a
SendQ cheia.

---

### Passo 3 — Caminho de leitura: `recv` → `appendToReadBuffer` → drenar linhas

**Status:** `done` — 2026-08-18

**Escrever:** o resto do `handleReadable(fd)`. Os quatro primeiros itens
abaixo **já entraram no passo 2** (ver a nota lá sobre o porquê) e ficam aqui só
como referência do contrato completo da função:

- ~~Um `recv` de `irc::RECV_CHUNK` por evento de prontidão~~ — nunca laço até
  `EAGAIN` (`ARCHITECTURE.md` §11 explica o porquê). **feito no passo 2**
- ~~`n == 0` → o peer fechou → desconecta.~~ **feito no passo 2**
- ~~`n < 0` com `EAGAIN`/`EWOULDBLOCK` → volta; não é erro nem desconexão.~~
  **feito no passo 2**
- ~~`EINTR` → volta; o próximo `poll` avisa de novo.~~ **feito no passo 2**
- `std::string(buf, n)` — nunca a forma de um argumento só.
- `appendToReadBuffer` retornando `false` → `ERROR :Request too long` +
  `disconnectClient`.
- Drenar com `while (!client.isDisconnecting() && client.extractCommand(line))`.

`handleLine` é **temporário só neste passo**, para a drenagem ficar observável
antes de o despachante existir. **Ele loga no `stdout` do servidor em vez de
ecoar pelo socket:** escrever para o cliente exige `sendToClient` + `POLLOUT`,
que são o passo 4, e abrir um `send()` cru aqui furaria o choke point único do
`ARCHITECTURE.md` §11 — a regra que faz a truncagem em 510 valer para todo byte
que sai. O eco pelo socket é o primeiro checkbox do passo 4.

**Testes — é aqui que o teste avaliado passa.** Versionados em
`tests/it/read_path.sh` (decisão D6), fora do build avaliado porque precisam de
socket de verdade. Rodar com o servidor parado, que o script sobe o dele:

```sh
./tests/it/read_path.sh          # usa a porta 6690; passe outra como argumento
```

O script afirma sobre o **log do servidor**, não sobre o que volta pelo socket:
até o passo 4 o servidor não tem como responder. Ele manda os três fragmentos
em pacotes separados, que é exatamente o que o Ctrl+D produz — a única coisa
que ele não consegue reproduzir é o Ctrl+D em si, que é ação de terminal e não
byte. Por isso o teste literal do subject continua sendo **feito na mão**:

```sh
nc -C 127.0.0.1 6667
com^Dman^Dd<Enter>               # o log tem que mostrar UMA linha: [command]
```

- [x] pacote parcial roteirizado vira um comando só
- [ ] `nc -C` com Ctrl+D **na mão** — único que não dá para roteirizar, fazer
      antes da avaliação
- [x] dois comandos num `printf` só viram dois comandos
- [x] 600 bytes + CRLF viram uma linha de 510
- [x] 5000 bytes sem `\n` → desconecta com `Request too long` (o texto vai para
      o fio como `ERROR :Request too long` no passo 4, quando houver como
      escrever; a razão já está guardada no `Client`)
- [x] `\n` sozinho (sem `\r`) funciona igual
- [x] valgrind sobre todo esse tráfego: 0 vazamentos, 0 erros
- [x] CPU ociosa em 0 ticks depois de todo o tráfego

---

### Passo 4 — Caminho de escrita: `sendToClient` + `POLLOUT`

**Status:** `done` — 2026-08-18

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

- [x] truncagem em 510 no `make test` (com as bordas: 510 passa inteiro, 511
      perde exatamente um byte, e o CRLF nunca é cortado no meio)
- [x] estouro de SendQ marca (e não deleta) no `make test`
- [x] eco do passo 3 agora sai por `sendToClient`, com CRLF correto —
      verificado no fio com `od -c` em `tests/it/write_path.sh`
- [x] CPU ociosa ainda em ~0% com cliente conectado e fila vazia (0 ticks em
      3 s — é aqui que um `POLLOUT` armado sem condição apareceria)
- [x] **SendQ estourando na vida real, não só no teste unitário:** um cliente
      que manda 4000 linhas e nunca lê levou a `closing fd 4 (SendQ exceeded)`.
      Isso é a linha "cliente que para de ler (`Ctrl+Z` no `nc`)" da Fase 4 do
      `TASKS.md`, antecipada de graça
- [x] valgrind sobre os dois caminhos: 0 vazamentos, 0 erros

**Duas coisas que este passo fechou e que estavam penduradas:**

1. **O flush best-effort no `reapDisconnected`.** É ele que faz a desconexão
   adiada valer a pena: o `ERROR :<reason>` é enfileirado num cliente **já
   marcado**, então sem esse `send` antes do `close` a mensagem morreria na
   fila. É o que o teste 3 do `write_path.sh` prova — o flood recebe o
   `ERROR :Request too long` que o passo 3 só sabia guardar.
2. **A recursão do `disconnectClient` deixou de ser teórica.** Agora que ele
   enfileira de verdade, o caminho `disconnectClient → sendToClient →
   queueOutput falha → disconnectClient` existe no código, e há um teste
   unitário cujo *sucesso é chegar na linha seguinte* (recursão infinita
   derrubaria o binário de teste).

---

### Passo 4.5 — Gancho de integração: o seam de canal de verdade

**Status:** `done` — 2026-08-18

**Estava no fim da fase (era o passo 9) e foi promovido para cá em 2026-08-15**,
quando o colega publicou `src/Channel.cpp` completo em `origin/domain`
(`da0165f`). O motivo da antecipação não sou eu — são os passos 5 a 8 que não
precisam disto. É ele: os handlers dele **linkam** contra o stub, mas
`getOrCreateChannel` devolve `NULL` lá, então o primeiro `JOIN` que ele testar
dá segfault. Ele consegue escrever, não consegue testar. Este é o ponto mais
cedo em que dá para destravá-lo, porque `broadcastToChannel` e
`broadcastToPeers` são construídos em cima do `sendToClient`, que é do passo 4.

**Primeira ação do passo: trazer o `Channel.cpp` para a árvore**
(`git merge origin/domain`). Sem ele, o `ServerChannels.cpp` não linka — foi
exatamente para isso que o stub existiu. Ficou decidido em 2026-08-15 **não**
mergear antes disso, para a `transport_fase_2` não carregar o trabalho da outra
trilha durante os passos 2 a 4.

- [x] `git merge origin/domain` — **sem um único conflito**. Os três arquivos
      que as duas trilhas tocaram (`docs/TASKS.md`, `tests/harness.hpp`,
      `tests/test_main.cpp`) foram auto-mergeados
- [x] `git rm src/ServerChannels.stub.cpp`
- [x] `src/ServerChannels.cpp` de verdade: `findChannel`, `getOrCreateChannel`,
      `removeChannel`, `broadcastToChannel`, `broadcastToPeers`,
      `sweepChannels`, `clearAllChannels`
- [x] chave do mapa `_channels` é `utils::toIrcLower(nome)` (D5), com
      `getName()` guardando a grafia original — testado com `#Dev`/`#dev`/`#DEV`
- [x] `disconnectClient` passa a transmitir o `QUIT` aos peers, **antes** da
      varredura: depois dela o conjunto de destinatários já estaria vazio
- [x] teste: cliente que sai de canal cheio não deixa ponteiro pendurado —
      `tests/it/channel_seam.sh`, sob valgrind
- [x] `make test` verde com os testes de `Channel` dele juntos: **335 passed**
      (187 meus + 131 dele + 17 novos do seam)

**O teste do ponteiro pendurado foi validado por mutação.** Quebrei o
`sweepChannels` de propósito e rodei o script: 19 leituras/escritas em memória
liberada, memória definitivamente vazada, `--error-exitcode=42` disparando. E o
checkbox "servidor sobreviveu" **continuou verde** — que é exatamente por que
esse teste roda sob valgrind: sem ele o bug é silencioso.

**Um andaime entrou junto, e sai no passo 5:** `handleLine` reconhece
`JOIN #chan`. Sem cmdJoin ninguém consegue entrar num canal pela rede, e sem
isso o `sweepChannels` — a função mais arriscada deste passo — ficaria com zero
cobertura. Ele morre junto com o resto do `handleLine` temporário.

`cmdNick` transmitindo a troca de nick aos peers (`includeOrigin = true`) e as 7
entradas de DOMAIN na `CommandTable.cpp` ficam para depois dos passos 6 e 5
respectivamente, já que dependem de código que ainda não existe nessa altura.

`broadcastToPeers` — o conjunto de destinatários deduplicado — é a peça
interessante desta fase e é minha: `NICK` e `QUIT` precisam alcançar todo mundo
que compartilha **algum** canal com a origem, **cada um exatamente uma vez**.
Repetir `broadcastToChannel` sobre os canais da origem entrega duplicado para
quem está em dois deles.

---

### Passo 5 — Despachante: parse, tabela, `421`, `451`

**Status:** `done` — 2026-08-18

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

- [x] comando desconhecido → `421`
- [x] comando conhecido antes do registro → `451` — ver a nota sobre o `JOIN`
- [x] mesmo comando em minúsculas → mesmo resultado
- [x] linha vazia não gera resposta
- [x] os seis permitidos antes do registro passam pela porta (silêncio, porque
      os corpos ainda estão vazios — que é o próprio ponto deste passo)
- [x] valgrind sobre todo esse tráfego: 0 vazamentos, 0 erros

**A ORDEM DAS DUAS CHECAGENS É UMA DECISÃO, não um detalhe.** A busca na tabela
vem **antes** da porta do registro. Um cliente não registrado que digita `FOO`
recebe `421`, não `451`: o comando não existe para ninguém, e responder "você
não se registrou" insinuaria que registrar primeiro faria o `FOO` funcionar. A
porta só se aplica a comandos que **existem**.

**Consequência que muda um teste desta seção:** enquanto o bloco do DOMAIN
estiver comentado na `CommandTable.cpp`, `JOIN #x` cai no `421` (comando
desconhecido), **não** no `451` que este passo previa. Ele vira `451` sozinho no
dia em que o `cmdJoin` entrar na tabela. O `tests/it/dispatch.sh` afirma o `421`
de hoje com um comentário dizendo isso — quando falhar, é o lembrete de
atualizar. O `451` está testado do mesmo jeito, via `PONG`, que está na tabela e
**não** está na lista dos seis permitidos antes do registro (`ARCHITECTURE.md`
§4). Isso não é descuido: este servidor nunca manda `PING` sem ser provocado,
então nenhum cliente tem motivo para mandar `PONG` antes de se registrar.

**Os testes de integração foram reescritos.** O `handleLine` temporário levou
consigo o eco e o log por linha, que eram a base de asserção do
`read_path.sh` e do `write_path.sh`. Agora os dois afirmam sobre o que volta
pelo socket — o `421` faz "o servidor viu exatamente um comando" ser observável
do lado do cliente, que é prova melhor do que uma linha de debug que nós mesmos
escrevíamos. O `channel_seam.sh` perdeu o andaime do `JOIN` e **sonda a tabela
antes de rodar**: hoje ele pula com aviso, e volta a rodar sozinho quando o
`cmdJoin` existir.

---

### Passo 6 — `cmdPass` / `cmdNick` / `cmdUser` + rajada `001`–`004`

**Status:** `done` — 2026-08-18

**Escrever:** `src/CommandsRegistration.cpp`. `461` sem parâmetros, `462`
depois de registrado, `464` + desconexão com senha errada, `431`/`432`/`433` no
`NICK`, e a rajada disparada **exatamente uma vez** na transição, controlada por
`welcomeSent()`.

**O arquivo de teste unitário mais rico da fase** (`tests/test_cmd_registration.cpp`),
tudo com `Server` e `Client` na pilha + `parseMessage`, verificando
`getOutputBuffer()`:

- [x] senha errada → `464` **e** `isDisconnecting()`, com o `ERROR` atrás
- [x] `NICK` antes do `PASS` → `451` — **mas não vindo do despachante**, ver a
      correção abaixo
- [x] alvo do numérico é `*` antes do registro e o nick depois
- [x] `USER` com 3 parâmetros → `461`
- [x] a rajada aparece uma única vez, na ordem `001 002 003 004`
- [x] segundo `PASS` depois de registrado → `462` (e segundo `USER` também)
- [x] nick inválido (`~foo`, começando com dígito, > 30) → `432`
- [x] troca de nick depois de registrado transmite `:antigo!user@host NICK
      :novo` com o prefixo **antigo**, sem repetir a rajada

**CORREÇÃO DO PLANO: aquele `451` não pode vir da porta do despachante.** O
`NICK` é justamente um dos seis comandos que a porta deixa passar — se ela o
barrasse, ninguém conseguiria se registrar nunca. O `451` de "`NICK` antes do
`PASS`" sai **do handler**, que é onde o `ARCHITECTURE.md` §7 o coloca, e a
regra é do RFC 2812 §3.1.1: a senha tem de vir antes de qualquer tentativa de
registro. É autorização antes de validação — `NICK` sem `PASS` nem chega a ser
validado como nick.

Uma consequência boa disso: como `NICK` e `USER` são recusados antes do `PASS`,
**o `PASS` nunca pode ser o comando que completa o registro**. Por isso ele não
chama o `completeRegistrationIfReady` — seria código morto fantasiado de rede de
segurança.

**Também entrou aqui, porque o `433` foi o primeiro a precisar:** o
`findClientByNick`, que estava declarado no seam desde a Fase 0 e nunca tinha
sido implementado. Ele é do contrato que a trilha DOMAIN também usa
(`PRIVMSG` para usuário, `INVITE`, `KICK`). A comparação insensível a
maiúsculas mora dentro dele, para nenhum chamador poder esquecer — e ele ignora
clientes já marcados para desconexão, cujo nick está efetivamente livre.

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

- [x] `433` com duas conexões vivas, insensível a maiúsculas
- [x] `464` fecha a conexão
- [x] registro completo devolve `001`–`004`
- [x] tudo isso versionado em `tests/it/registration.sh` (8 casos), em vez de
      comandos soltos para colar no terminal
- [x] valgrind sobre registro, colisão, senha errada e troca de nick: limpo

---

### Passo 7 — `PING`/`PONG`, `QUIT`, `CAP`

**Status:** `done` — 2026-08-18

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

- [x] `PING token` devolve o mesmo token (e um token com espaços, vindo como
      parâmetro trailing, volta inteiro)
- [x] `QUIT` colado com outra linha: a segunda linha não é despachada
- [x] o mesmo teste sob valgrind, sem uso de memória liberada
- [x] `CAP LS 302` não derruba nem gera erro no cliente
- [x] `PING` sem token → `461`, e não um `409` inventado (ver abaixo)
- [x] tudo em `tests/it/session.sh`, com o servidor **rodando sob valgrind**

**`PING` sem token usa `461`, não `409`.** O RFC 2812 responde `409
ERR_NOORIGIN`, que **não está** na tabela de numerics que as duas trilhas
acordaram (`ARCHITECTURE.md` §6). Entre inventar um código e usar um que já
existe e descreve o mesmo fato — falta um parâmetro — escolhi o `461`. Se o
irssi reclamar na Fase 3, a correção é adicionar o `409` à tabela **e** avisar,
não improvisar no handler.

**A validação por mutação corrigiu o que eu achava sobre este passo.** Removi a
guarda `isDisconnecting()` do laço de drenagem e rodei o teste: o valgrind
ficou **limpo**, e só a asserção de comportamento falhou. Ou seja, os dois
mecanismos protegem coisas diferentes, e vale saber qual é qual na avaliação:

| Mecanismo | Do que protege |
|---|---|
| Desconexão **adiada** (`disconnectClient` marca, `reapDisconnected` deleta no fim da iteração) | **Segurança de memória.** É ele que faz o `Client&` continuar válido durante todo o laço de drenagem — sem ele, sim, seria use-after-free |
| **Guarda** no laço de drenagem | **Correção.** Sem ela o servidor despacha alegremente a linha depois do `QUIT`: um `PRIVMSG` entregue por quem já saiu, ou um `JOIN` de um cliente prestes a ser varrido de todos os canais |

O `ARCHITECTURE.md` §4 descreve os dois juntos como prevenção de
use-after-free. Estritamente, o use-after-free só apareceria se a desconexão
fosse imediata; a guarda evita o comportamento errado, não a memória inválida.

---

### Passo 8 — Endurecimento e fechamento da fase

**Status:** `done` — 2026-08-18

- [x] bytes NUL e lixo binário (`head -c 200 /dev/urandom`) não derrubam — e o
      NUL no meio de `PI\0NG token` é descartado, com o comando funcionando
- [x] 50 conexões simultâneas num laço (todas registradas, fds voltando ao
      normal depois), mais 30 conexões abertas e fechadas na hora
- [x] `valgrind --leak-check=full` com clientes conectados no SIGINT:
      **27 clientes vivos, 0 bytes in use at exit, 0 erros**
- [x] leitura de todo retorno de `recv`/`send`/`accept`/`poll`: nenhum ignorado
- [x] `TASKS.md` atualizado (Fase 2 → `done`)
- [x] decisões D1–D7 anotadas no `ARCHITECTURE.md`
- [x] `README.md` com como rodar `tests/it/*.sh`

**A auditoria de syscall passou, com duas exceções deliberadas.** Todo retorno
de `socket`, `setsockopt`, `fcntl`, `bind`, `listen`, `poll`, `accept`, `recv` e
`send` é lido. Ficam de fora `signal` (devolve o handler anterior, que não nos
interessa) e `close` — e o motivo do `close` está escrito no código, porque é o
que um avaliador pergunta: um `close` que falha dá `EBADF`, e aí não há o que
fazer, ou `EINTR`, em que no Linux **o descritor é fechado assim mesmo**.
Repetir a chamada fecharia um número que o kernel pode já ter entregue a outra
conexão — bem pior do que perder o erro.

**Um teste meu estava errado, não o código.** Escrevi que uma linha de 5000
bytes **com** CRLF deveria ser truncada em 510. Ela não é: se o CRLF chegar num
segmento TCP separado, o `recv` anterior deixa o buffer com 5000 bytes e nenhum
terminador, e aí a política do `ARCHITECTURE.md` §11 manda desconectar — o
servidor não tem como adivinhar que vem um CRLF depois, e uma linha desse
tamanho é ilegal de qualquer jeito (o RFC limita em 512). A fronteira da
truncagem é o `MAX_READ_BUFFER`, não o 510. Corrigi o teste para 4000 bytes,
que é o caso que a política realmente descreve.

---

## 5. Arquivos que esta fase cria ou toca

| Arquivo | Ação | Passo |
|---|---|---|
| `src/Server.cpp` | novo | 1–4 |
| `src/ServerChannels.stub.cpp` | novo, **descartável** | 1 (apagado no 4.5) |
| `src/ServerChannels.cpp` | novo | 4.5 |
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

| O quê | Quem destrava | Desde | Situação |
|---|---|---|---|
| `src/Channel.cpp` não existe → gancho de integração parado | DOMAIN | — | **resolvido** 2026-08-15, `origin/domain` `da0165f` |
| D1–D6 aceitas sem o colega ver; D2 e D5 precisam ser comunicadas | Eduardo | 2026-08-15 | **resolvido** 2026-08-15, DOMAIN confirmou tudo |
| `src/ServerChannels.cpp` de verdade não existe → os handlers de DOMAIN linkam contra o stub e dão segfault no primeiro `JOIN` | TRANSPORT (passo 4.5) | 2026-08-15 | **resolvido** 2026-08-18, passo 4.5 — o stub foi apagado e o seam é real |
| D5 ainda não anotado no `ARCHITECTURE.md` §5 (passo 0) | Eduardo | 2026-08-15 | **resolvido** 2026-08-15, commit `c5a04ef` (§5, subseção Casemapping) |
