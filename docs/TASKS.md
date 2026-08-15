# TASKS — ft_irc

Superfície de sincronização diária entre as duas sessões de IA. **Atualize no
mesmo commit que muda o código.**

Rótulos em português; identificadores, nomes de arquivo e de comandos em inglês
(são artefatos do código).

**Status:** `todo` → `doing` → `done` (funciona e tem teste) → `integrated`
(mergeado na `main`).

> A definição de `integrated` era "mergeado na `main` **via PR, revisado pelo
> colega**". A fase 1 (TRANSPORT + SHARED) foi para a `main` **direto, sem PR**,
> de comum acordo: o colega ainda não tinha começado a trilha DOMAIN, então não
> havia nada em paralelo para revisar. A regra do PR volta a valer da fase 2 em
> diante, quando as duas trilhas passam a se tocar.

**Donos:** `TRANSPORT` (Eduardo) · `DOMAIN` (colega) · `SHARED` (quem pegar
primeiro — marque seu nome ao começar) · `BOTH` (sessão conjunta).

---

## Fase 0 — Fundação

| Item | Dono | Status |
|---|---|---|
| Estrutura do repositório + `.gitignore` | BOTH | done |
| `Makefile` (NAME/all/clean/fclean/re/test, sem relink) | BOTH | done |
| Headers de contrato compilando | BOTH | done |
| `main.cpp` — validação de argumentos | BOTH | done |
| Harness de teste (`tests/test_main.cpp`) | BOTH | done |
| `README.md` / `ARCHITECTURE.md` / `PLANO.md` / `TASKS.md` | BOTH | done |
| Seção "Testing" no `README.md` (os dois binários, matriz de argumentos) | BOTH | done |
| `Limits.hpp` + seção 11 do `ARCHITECTURE.md` (limites e bounds) | BOTH | done |
| Os dois leram o `ARCHITECTURE.md` e aceitaram o seam | BOTH | todo |

---

## Fase 1 — Lógica pura (sem sockets)

| Item | Dono | Status |
|---|---|---|
| `utils::toIrcLower` / `equalsIgnoreCase` | SHARED (Eduardo) | done |
| `utils::split` | SHARED (Eduardo) | done |
| `utils::toString` / `parseInt` | SHARED (Eduardo) | done |
| `utils::isValidNickname` | SHARED (Eduardo) | done |
| `utils::isValidChannelName` | SHARED (Eduardo) | done |
| `parseMessage` — prefixo, comando, params | SHARED (Eduardo) | done |
| `parseMessage` — regra do trailing (`:`) | SHARED (Eduardo) | done |
| `Client` — OCF, getters/setters, `prefix()` | TRANSPORT | done |
| `Client` — flags de registro (`isRegistered`) | TRANSPORT | done |
| `Client::appendToReadBuffer` / `extractCommand` | TRANSPORT | done |
| `Client` — teto do buffer de leitura (`MAX_READ_BUFFER`, retorna `false`) | TRANSPORT | done |
| `Client` — truncar linha em `MAX_PAYLOAD_LEN` (510) | TRANSPORT | done |
| `Client` — descartar NUL e `\r`/`\n` soltos | TRANSPORT | done |
| `Client` — buffer de saída (`queueOutput`/`consumeOutput`) | TRANSPORT | done |
| `Client` — teto da fila de saída (`MAX_OUTPUT_QUEUE`, SendQ) | TRANSPORT | done |
| `Client` — `isDisconnecting` / `markDisconnecting` | TRANSPORT | done |
| `Channel` — OCF, nome, tópico | DOMAIN | done |
| `Channel` — membros (`add`/`remove`/`isMember`) | DOMAIN | done |
| `Channel` — operadores | DOMAIN | done |
| `Channel` — lista de convites (guarda `Client*`, não nick) | DOMAIN | done |
| `Channel` — modos `i` e `t` | DOMAIN | todo |
| `Channel` — modo `k` (key) | DOMAIN | todo |
| `Channel` — modo `l` (limit) | DOMAIN | todo |
| `Channel::modeString` | DOMAIN | todo |
| `Replies::numeric` / `Replies::fromClient` | SHARED (Eduardo) | done |

---

## Fase 2 — Servidor vivo

| Item | Dono | Status |
|---|---|---|
| `socket` / `setsockopt` / `bind` / `listen` | TRANSPORT | todo |
| `fcntl(fd, F_SETFL, O_NONBLOCK)` em todo fd | TRANSPORT | todo |
| **Laço de `poll()` (sessão conjunta)** | BOTH | todo |
| `accept` de novo cliente + entrada no `_pollFds` | TRANSPORT | todo |
| `recv` → `appendToReadBuffer` → drenar linhas | TRANSPORT | todo |
| `send` não bloqueante com `POLLOUT` (armado só se há saída) | TRANSPORT | todo |
| `sendToClient` trunca em 510 antes do CRLF | TRANSPORT | todo |
| `broadcastToPeers` — destinatários únicos (NICK/QUIT) | TRANSPORT | todo |
| `disconnectClient` marca; `reapDisconnected` deleta no fim | TRANSPORT | todo |
| Despacho para de extrair linhas se `isDisconnecting()` | TRANSPORT | todo |
| `buildCommandTable` + despacho | TRANSPORT | todo |
| Porta do registro no despacho (`451`) | TRANSPORT | todo |
| **`signal(SIGPIPE, SIG_IGN)`** — sem isso o processo morre | TRANSPORT | todo |
| `signal` para `SIGINT` (shutdown sem vazar) | TRANSPORT | todo |
| `EAGAIN`/`EWOULDBLOCK` não é erro nem desconexão | TRANSPORT | todo |
| `EINTR` repete a chamada | TRANSPORT | todo |
| `POLLHUP` / `POLLERR` / `POLLNVAL` tratados | TRANSPORT | todo |
| `cmdPass` (+ `462`, `464`) | TRANSPORT | todo |
| `cmdNick` (+ `431`, `432`, `433`) | TRANSPORT | todo |
| `cmdUser` (+ `461`, `462`) | TRANSPORT | todo |
| Rajada de boas-vindas `001`–`004` | TRANSPORT | todo |
| `cmdPing` / `cmdPong` | TRANSPORT | todo |
| `cmdQuit` | TRANSPORT | todo |
| `CAP` não derruba o servidor | TRANSPORT | todo |

---

## Fase 3 — Comandos contra o irssi

| Item | Dono | Status |
|---|---|---|
| `cmdJoin` — canal novo, criador vira operador | DOMAIN | todo |
| `cmdJoin` — sequência `JOIN`/`332`/`353`/`366` | DOMAIN | todo |
| `cmdJoin` — múltiplos canais (`#a,#b key1,key2`) | DOMAIN | todo |
| `cmdJoin` — checagem de `+i`, `+k`, `+l` (`473`/`475`/`471`) | DOMAIN | todo |
| `cmdPart` (+ `442`, canal vazio é removido) | DOMAIN | todo |
| `cmdPrivmsg` — para canal | DOMAIN | todo |
| `cmdPrivmsg` — para usuário (+ `401`, `411`, `412`) | DOMAIN | todo |
| `cmdTopic` — ver e alterar (+ `331`, `332`, `+t` → `482`) | DOMAIN | todo |
| `cmdKick` (+ `441`, `442`, `482`) | DOMAIN | todo |
| `cmdInvite` (+ `341`, `401`, `443`, `482`) | DOMAIN | todo |
| `cmdMode` — parser de flags (`+it`, `-ik`, params) | DOMAIN | todo |
| `cmdMode` — `i` | DOMAIN | todo |
| `cmdMode` — `t` | DOMAIN | todo |
| `cmdMode` — `k` | DOMAIN | todo |
| `cmdMode` — `o` | DOMAIN | todo |
| `cmdMode` — `l` | DOMAIN | todo |
| `cmdMode` — consulta (`324`) e flag desconhecida (`472`) | DOMAIN | todo |
| Confirmar `341` e `366` contra o irssi e anotar no `ARCHITECTURE.md` | BOTH | todo |

---

## Fase 4 — Endurecimento

| Item | Dono | Status |
|---|---|---|
| Pacote parcial via `nc -C` (teste do subject) | BOTH | todo |
| Vários comandos num pacote só | BOTH | todo |
| Regressão: linha > 512 truncada (implementado na Fase 1) | BOTH | todo |
| `PRIVMSG` de 504 bytes + prefixo sai truncado em 510 | BOTH | todo |
| `QUIT` colado com outra linha no mesmo pacote | BOTH | todo |
| Cliente que para de ler (`Ctrl+Z` no `nc`) estoura SendQ | BOTH | todo |
| Bytes NUL e lixo binário não derrubam | BOTH | todo |
| Cliente morto (`kill -9`) sem `QUIT` | BOTH | todo |
| Muitos clientes simultâneos | BOTH | todo |
| `valgrind` sem vazamento | BOTH | todo |
| Nenhum retorno de syscall ignorado | BOTH | todo |
| Ensaio: Eduardo explica a trilha DOMAIN | TRANSPORT | todo |
| Ensaio: colega explica a trilha TRANSPORT | DOMAIN | todo |

---

## Bloqueios e dúvidas em aberto

Anote aqui o que está travando, para o outro ver sem precisar perguntar.

| O quê | Quem | Desde |
|---|---|---|
| — | — | — |
