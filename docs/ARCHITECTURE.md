# ARCHITECTURE

**This file is the contract.** Two developers work in two separate AI sessions
that cannot see each other's decisions. This document plus `TASKS.md` are the
only shared brain. If you change anything in here, say so in the PR — a change
here is a change to the other person's assumptions.

Source of truth for requirements: [`ft_irc_requirements.md`](ft_irc_requirements.md).
If this file ever disagrees with the subject, the subject wins.

---

## 1. Core design principle

**Logic takes strings and objects. Logic never touches file descriptors.**

Everything follows from this:

- The parser, the validation helpers, the `Channel` model, the read-buffer
  reassembly, and the registration state machine are all testable with
  constructed strings, before a single socket exists.
- Only accept / recv / send / poll / disconnect are genuinely network code.
- A command handler that wants to reach the world calls a `Server` method. It
  never sees an `int fd`.

If you find yourself needing an fd inside a handler, the seam is wrong — fix
the seam, don't smuggle the fd through.

---

## 2. Hard conventions

| Rule | Detail |
|---|---|
| Standard | C++98 only. No `auto`, `nullptr`, range-for, `std::to_string`, `<unordered_map>`, lambdas, `>>` without a space in nested templates. Use `utils::toString` instead of `std::to_string`. |
| Flags | `c++ -Wall -Wextra -Werror -std=c++98`. Warnings are build failures; do not silence them with casts you don't understand. |
| Libraries | Standard library only. No Boost, no external anything. |
| I/O | Exactly **one** `poll()` for every fd, including the listening socket. Reading or writing any fd without going through that poll is an automatic 0. |
| Blocking | Every socket is `O_NONBLOCK`. Never assume `send()` wrote everything — that is what `Client`'s output buffer is for. |
| Line endings | Every message the server sends ends with `\r\n`. |
| CRLF ownership | `Server::sendToClient` appends `\r\n` **itself**. Never include it in a string you pass to it. |
| Crashes | The server must never crash or exit unexpectedly. Every `recv`/`send`/`accept` return value is checked. No unchecked `.at()`, no dereferenced `NULL`, no `std::vector` invalidated mid-iteration. |
| SIGPIPE | `signal(SIGPIPE, SIG_IGN)` at startup, before anything else. Writing to a socket whose peer already closed raises SIGPIPE, and its default action **terminates the process** — an instant grade 0, triggered by something as ordinary as a client being `kill -9`'d mid-broadcast. `signal` is in the subject's allowed-function list. |
| `EAGAIN` | On a non-blocking fd, `recv`/`send` returning -1 with `errno == EAGAIN` (or `EWOULDBLOCK`) means "nothing right now" — **not** an error and **not** a disconnect. Treating it as failure is the classic non-blocking bug. |
| `EINTR` | `poll`/`recv`/`send` returning -1 with `errno == EINTR` means a signal interrupted the call. Retry; do not abort and do not disconnect. |
| `revents` | Check `POLLHUP`, `POLLERR` and `POLLNVAL` on every fd, not just `POLLIN`/`POLLOUT`. Ignoring them spins the loop on a dead fd. `POLLNVAL` means our own fd bookkeeping is wrong — drop the entry rather than crash. |
| Limits | Every buffer has a bound, from `include/Limits.hpp`. See section 11. |
| Naming | Code, identifiers, comments and commit messages in **English**. Coordination docs (`PLANO.md`, `TASKS.md`) in Portuguese. |

---

## 3. Modules and ownership

| Module | Files | Owner | Notes |
|---|---|---|---|
| Parsed message | `Message.hpp` | shared | Parser output struct + `parseMessage`. |
| Utilities | `Utils.hpp` | shared middle | Validation, casemapping, split, int conversion. Grab these when blocked. |
| Client | `Client.hpp` | transport | fd, identity, registration flags, read + write buffers. |
| Server | `Server.hpp` | transport | Socket setup, the poll loop, the seam. |
| Channel | `Channel.hpp` | domain | Members, operators, invites, modes i/t/k/l. |
| Commands | `Command.hpp` | split | `cmdPass/Nick/User/Quit/Ping/Pong` = transport. `cmdJoin/Part/Privmsg/Kick/Invite/Topic/Mode` = domain. |
| Replies | `Replies.hpp` | shared | Numeric codes and the two builders. Nobody invents a format. |
| Limits | `Limits.hpp` | shared | Protocol and buffer bounds. No magic numbers at call sites. |

**The poll loop itself is built jointly in one session** — see `PLANO.md`,
Phase 2. It is the most novel and most-probed piece; both of us need to be able
to explain it line by line.

### What is *not* in the contract

Private and free to change without asking: the read-buffer internals behind
`Client::appendToReadBuffer` / `extractCommand`, the parser's internals, and
everything in `Server`'s private section. Only the public shapes above are
negotiated.

### Ownership of memory

`Server` owns every `Client` and every `Channel` (it `new`s and `delete`s them).
`Channel` stores **non-owning** `Client*` in its member, operator and invite
sets. A client must therefore be removed from every channel *before* it is
deleted, and `Server::reapDisconnected` is the only place that deletion ever
happens.

The invite list stores `Client*` too, **not nicknames**. Nicknames would lose
the invite the moment the invitee runs `/nick`: invite `alice`, she becomes
`bob`, her `JOIN` looks up `bob` and finds nothing. Pointer identity survives a
nick change for free. The price is that the disconnect sweep must visit *all*
channels, including ones the client was invited to but never joined.

`Client::getFd()` is **private, with `Server` as the only friend**. That turns
"logic never touches file descriptors" from a convention into a compile error:
a handler physically cannot reach the fd.

---

## 4. The seam

The complete list of what a command handler may call on `Server`:

```cpp
Client  *findClientByNick(const std::string &nickname);
Channel *findChannel(const std::string &name);
Channel *getOrCreateChannel(const std::string &name);
void     removeChannel(const std::string &name);
void     sendToClient(Client &client, const std::string &line);
void     broadcastToChannel(Channel &channel, const std::string &line,
                            const Client *except);
void     broadcastToPeers(Client &origin, const std::string &line,
                          bool includeOrigin);
void     disconnectClient(Client &client, const std::string &reason);
const std::string &getPassword() const;
const std::string &getServerName() const;
```

`broadcastToPeers` exists because `NICK` and `QUIT` must reach everyone who
shares *any* channel with the origin, **each of them exactly once**. Looping
`broadcastToChannel` over the origin's channels delivers duplicates to anyone
present in two of them, so the recipient set has to be collected and
deduplicated first. `includeOrigin` is true for `NICK` (irssi wants its own
change echoed back) and false for `QUIT`.

> **Changed 2026-08-18 (transport):** `origin` was `const Client &` and is now
> `Client &`. Note the contrast with `except` in `broadcastToChannel`, which
> stays const: `except` is only ever *compared*, while `includeOrigin` means
> `broadcastToPeers` may have to *deliver* to the origin — and `sendToClient`
> takes a non-const reference. The old signature bought nothing and cost a
> `const_cast` in the body.
>
> **This one did not need a conversation first, unlike the rest of this
> section.** The rule exists to stop the two tracks diverging *silently*, and
> this change cannot: a non-const `Client &` binds to it fine, so every
> existing caller keeps compiling, and the only way to break is to pass a
> `const Client &` — which is a compile error, not a runtime surprise. Command
> handlers receive `Client &sender` (non-const) anyway, and the only commands
> that call this are `NICK` and `QUIT`, both on the transport side.

### Disconnection is deferred, never immediate

`disconnectClient` **marks** a client; it does not delete it. The poll loop
reaps every marked client at the end of the iteration: one best-effort `send()`
to flush what is queued, removal from all channels and invite lists, `close`,
`delete`.

Deleting inside the handler would be a use-after-free, and the way to trigger
it is mundane. One TCP packet can carry:

```
QUIT :bye\r\nPRIVMSG #chan :hello\r\n
```

The dispatch loop extracts `QUIT`, runs `cmdQuit`, and if that deleted the
`Client` the loop would then process `PRIVMSG` through a dangling reference.
Deferring also means queued output can still be flushed, which is impossible
once the fd is closed.

So: the `Client&` **remains valid** after `disconnectClient` returns. The
handler should return promptly, and **the dispatch loop must stop extracting
lines from a client once `isDisconnecting()` is true.**

`disconnectClient` must itself **return immediately when the client is already
marked**: it queues `ERROR :<reason>`, so on a client whose output queue is
already full that queueing fails, routes straight back into `disconnectClient`,
and recurses until the stack is gone.

That is the whole surface. Every method on it is a coupling point between the
two of us — adding one is a conversation, not a commit.

### Handler signature

```cpp
typedef void (*CommandHandler)(Server &server, Client &sender,
                               const Message &msg);
```

Dispatch is `std::map<std::string, CommandHandler>`, keyed by the **uppercased**
command name (IRC commands are case-insensitive). No command class hierarchy,
no factory. A map lookup is the entire mechanism.

The dispatcher, not the handler, enforces registration: if
`!sender.isRegistered()` and the command is not `PASS`/`NICK`/`USER`/`QUIT`/
`PING`/`CAP`, reply `451 ERR_NOTREGISTERED` and stop.

---

## 5. Message grammar

```
[ ':' prefix SPACE ] command *( SPACE middle ) [ SPACE ':' trailing ]
```

- Max 512 bytes **including** `\r\n`, so 510 for the payload (RFC 2812 §2.3).
  This is enforced in both directions — see section 11.
- The **trailing** parameter begins at the first `" :"` and runs to end of
  line, spaces included. It is the last element of `params`, with the `:`
  stripped. `hasTrailing` is kept because `":"` alone is a legal empty
  trailing param and must be distinguishable from no param at all.
- Clients normally send no prefix; the server adds one on everything it
  relays.

```
"PRIVMSG #chan :hello world"   -> cmd=PRIVMSG params=["#chan","hello world"]
"JOIN #a,#b key1,key2"         -> cmd=JOIN    params=["#a,#b","key1,key2"]
"MODE #chan +o bob"            -> cmd=MODE    params=["#chan","+o","bob"]
```

Malformed input yields an empty `command`: ignore the line, do not reply.

### Casemapping (RFC 2812 §2.2)

Nicknames and channel names are case-insensitive, and `{}|^` are the lowercase
forms of `[]\~`. Plain `std::tolower` is **wrong** here — use
`utils::toIrcLower`.

**Where that is enforced for channels** (Phase 2 decision, binding on both
tracks). `Server::_channels` is keyed by `utils::toIrcLower(name)`, while
`Channel::getName()` keeps the **original spelling** for display. So
`findChannel`, `getOrCreateChannel` and `removeChannel` are case-insensitive at
the seam, and a handler never normalises a channel name itself — it passes
whatever the client sent and gets the right channel back.

Getting this wrong is silent rather than loud: without the normalisation,
`JOIN #Dev` followed by `PRIVMSG #dev` creates *two* channels, each with one
member, and both clients sit in a room that looks empty. Nicknames get the same
treatment through `utils::equalsIgnoreCase` in `findClientByNick`.

### Name validation

`utils::isValidNickname` follows RFC 2812 §2.3.1: the first character is a
letter or one of the specials ``[ \ ] ^ _ ` { | }``, and later characters may
also be digits or `-`. Note that `~` is **not** in that special set, so it never
appears in a legal nickname.

Two decisions, taken in Phase 1 and binding on both tracks:

- **Nicknames are capped at 30, not the RFC's 9.** The RFC's figure is from
  1988; every deployed server raised it, and irssi fills the nickname from the
  system username, so a 9-character cap turns away real logins before the user
  types anything. Channel names keep the RFC's own 50. Both constants live in
  `Limits.hpp` — see section 11.
- **`#` is the only channel prefix.** RFC 2812 also lists `&`, `+` and `!`. `+`
  means "supports no modes", which contradicts the `MODE` work the subject
  requires, and `!` needs generated channel IDs. `&` means *server-local* — and
  since the subject forbids server-to-server links, every channel here is
  already server-local, so `&` would be a second prefix with identical
  behaviour. `&foo` fails validation and the caller answers `403`.

---

## 6. Reply formats

Two shapes, both built in `Replies.hpp`, neither ending in CRLF:

```
:<servername> <code> <target> <args>        numeric(...)
:<nick>!<user>@<host> <COMMAND> <args>      fromClient(...)
```

`<target>` is the recipient's nickname, or `*` when they are not registered yet.

### Numerics we use

Taken from **RFC 2812 §5**. The column shows everything after `<target>`.
Do not invent codes and do not reword these strings.

| Code | Name | Arguments after target |
|---|---|---|
| 001 | RPL_WELCOME | `:Welcome to the Internet Relay Network <nick>!<user>@<host>` |
| 002 | RPL_YOURHOST | `:Your host is <servername>, running version <ver>` |
| 003 | RPL_CREATED | `:This server was created <date>` |
| 004 | RPL_MYINFO | `<servername> <version> <user modes> <channel modes>` |
| 324 | RPL_CHANNELMODEIS | `<channel> <mode> <mode params>` |
| 331 | RPL_NOTOPIC | `<channel> :No topic is set` |
| 332 | RPL_TOPIC | `<channel> :<topic>` |
| 341 | RPL_INVITING | `<channel> <nick>` — see note below |
| 353 | RPL_NAMREPLY | `= <channel> :<prefixed nicks separated by spaces>` |
| 366 | RPL_ENDOFNAMES | `<channel> :End of NAMES list` |
| 401 | ERR_NOSUCHNICK | `<nickname> :No such nick/channel` |
| 403 | ERR_NOSUCHCHANNEL | `<channel> :No such channel` |
| 404 | ERR_CANNOTSENDTOCHAN | `<channel> :Cannot send to channel` |
| 411 | ERR_NORECIPIENT | `:No recipient given (<command>)` |
| 412 | ERR_NOTEXTTOSEND | `:No text to send` |
| 421 | ERR_UNKNOWNCOMMAND | `<command> :Unknown command` |
| 431 | ERR_NONICKNAMEGIVEN | `:No nickname given` |
| 432 | ERR_ERRONEUSNICKNAME | `<nick> :Erroneous nickname` |
| 433 | ERR_NICKNAMEINUSE | `<nick> :Nickname is already in use` |
| 441 | ERR_USERNOTINCHANNEL | `<nick> <channel> :They aren't on that channel` |
| 442 | ERR_NOTONCHANNEL | `<channel> :You're not on that channel` |
| 443 | ERR_USERONCHANNEL | `<user> <channel> :is already on channel` |
| 451 | ERR_NOTREGISTERED | `:You have not registered` |
| 461 | ERR_NEEDMOREPARAMS | `<command> :Not enough parameters` |
| 462 | ERR_ALREADYREGISTRED | `:Unauthorized command (already registered)` |
| 464 | ERR_PASSWDMISMATCH | `:Password incorrect` |
| 471 | ERR_CHANNELISFULL | `<channel> :Cannot join channel (+l)` |
| 472 | ERR_UNKNOWNMODE | `<char> :is unknown mode char to me` |
| 473 | ERR_INVITEONLYCHAN | `<channel> :Cannot join channel (+i)` |
| 475 | ERR_BADCHANNELKEY | `<channel> :Cannot join channel (+k)` |
| 482 | ERR_CHANOPRIVSNEEDED | `<channel> :You're not channel operator` |

> **462 is spelled `ERR_ALREADYREGISTRED` in the RFC** — the missing `E` is in
> the standard, not a typo of ours.

### The welcome burst, concretely

Decided in phase 2 and binding on both tracks, because these strings are what
irssi is matched against:

| What | Value | Why |
|---|---|---|
| `<servername>` | the fixed string `ircserv` | Deriving it from the machine would need `gethostname`, which is **not** in the subject's allowed-function list. (That list has `gethostbyname`, which is a different call: name → IP address.) |
| `<version>` | `1.0` | Appears in 002 and 004. |
| 003's date | `__DATE__ " " __TIME__` | Filled in by the compiler: no syscall, no stored state, and trivially explainable. |
| 004's payload | `ircserv 1.0 - itkol` | `-` is the user-mode field: we implement none. `itkol` is every channel mode we do implement. |

So a registered client sees exactly:

```
:ircserv 001 alice :Welcome to the Internet Relay Network alice!alice@127.0.0.1
:ircserv 002 alice :Your host is ircserv, running version 1.0
:ircserv 003 alice :This server was created <compile date>
:ircserv 004 alice ircserv 1.0 - itkol
```

The burst fires **once**, on the transition into registered, and is guarded by
`Client::welcomeSent()` rather than by `isRegistered()` — the latter stays true
forever afterwards and would replay the welcome on every subsequent `NICK`.

> **`PING` with no token answers 461, not 409.** RFC 2812 uses
> `409 ERR_NOORIGIN`, which is not in the table above. Rather than invent a
> code, the server reuses `461`: a missing token is a missing parameter. If
> irssi objects in phase 3, add 409 to the table **and say so** — do not
> improvise in the handler.

> **Two codes where RFC 1459 and RFC 2812 disagree.** For 341, RFC 2812 says
> `<channel> <nick>` while RFC 1459 says `<nick> <channel>`, and most real
> servers follow 1459. For 366, RFC 2812 says `End of NAMES list` while much
> deployed software sends `End of /NAMES list`. Both are cosmetic to irssi.
> **Pick one, verify against irssi in Phase 3, and write the decision here.**

### Non-numeric messages the server relays

Sent with `fromClient(...)`, prefixed with the *originating* client:

```
:nick!user@host JOIN #chan
:nick!user@host PART #chan :reason
:nick!user@host PRIVMSG #chan :text
:nick!user@host PRIVMSG othernick :text
:nick!user@host KICK #chan victim :reason
:nick!user@host TOPIC #chan :new topic
:nick!user@host MODE #chan +o bob
:nick!user@host NICK :newnick
:nick!user@host QUIT :reason
```

Server-originated, not client-prefixed:

```
PING :<token>                    server -> client
:<servername> PONG <servername> :<token>    reply to a client PING
ERROR :<reason>                  sent immediately before we close a socket
```

### JOIN success sequence

The exact order irssi expects. Getting this wrong is the usual reason a
channel window opens empty:

```
:nick!user@host JOIN #chan          (broadcast to all members, sender included)
:server 332 nick #chan :<topic>     (or 331 if no topic)
:server 353 nick = #chan :@op nick2 nick3
:server 366 nick #chan :End of NAMES list
```

In 353, channel operators are prefixed with `@`. We do not implement voice, so
no `+` prefixes.

---

## 7. Registration flow

`PASS` → `NICK` → `USER`, then the welcome burst. A client is registered when
all three of `hasPass`, `hasNick`, `hasUser` are true.

Rules:

- `PASS` before registration only; after registration reply 462.
- Wrong password → 464, then disconnect.
- If `NICK`/`USER` arrive without a correct `PASS` first, the client can never
  register. Reply 451 and keep the connection until they get it right or quit.
- On the transition to registered, send **001, 002, 003, 004** in that order.
  irssi treats 001 as "connected"; without it, it will sit waiting.
- Nickname collisions are case-insensitive (`utils::equalsIgnoreCase`) → 433.

### What irssi actually does on connect

Test against these, not against a clean-room reading of the RFC:

1. Sends `CAP LS 302` **before** `PASS`, and **waits for the answer**. We
   implement no capabilities, so the answer is an empty list:

   ```
   :ircserv CAP * LS :        (CAP REQ -> ":ircserv CAP * NAK :<what was asked>")
   ```

   irssi replies `CAP END` to that and proceeds with `PASS`/`NICK`/`USER`.

   > **This is decision D17 and it supersedes D2 (`docs/FASE3.md`).** Phase 2
   > decided to ignore `CAP` *silently*, to keep a `421` out of irssi's status
   > window — the subject requires the reference client to connect "without
   > encountering any error". The reasoning was right; the remedy was not.
   > Pointed at irssi 1.4.5, the silent handler made it print
   > `Waiting for CAP LS response...` and stop: it never sent `PASS`, `NICK` or
   > `USER`, so **nobody could register at all**. Silence is not a non-answer
   > to a real client, it is a stall. Every unit test we had was green
   > throughout — see section 9.

   `CAP` is still **not** a registration step, still passes the pre-registration
   gate, and still must not crash anything.
2. Sends `PASS`, `NICK`, `USER` back to back — often in a **single TCP packet**.
   The read buffer must split that into three commands.
3. Sends `PING` periodically and expects a `PONG` with the same token. Miss it
   and irssi eventually reports a timeout.
4. Sends `QUIT :reason` on `/quit`. Broadcast it to every channel the client
   was in, then close.

`PING`/`PONG`, `QUIT` and `CAP` are not in the subject's command list, but the
subject requires that the reference client "connect without encountering any
error". These are what that costs in practice.

---

## 8. Channel modes

| Mode | Parameter | Meaning |
|---|---|---|
| `i` | none | Invite-only. JOIN without an invite → 473. |
| `t` | none | Only operators may change the topic → 482. |
| `k` | key (on set) | Channel key. Wrong or missing key → 475. |
| `o` | nickname | Grant/revoke operator. Target must be a member → 441. |
| `l` | limit (on set) | User limit. Channel full → 471. |

- Only channel operators may run `MODE`, `KICK`, `INVITE`.
- Changing TOPIC requires an operator only under +t. Querying the topic does not.
- The first user to `JOIN` a new channel becomes its operator.
- A mode string may carry several flags (`+it`, `-ik`). Parameters are consumed
  left to right, only by the flags that take one. Unknown flag → 472.
- Removing a mode that takes a parameter (`-k`, `-l`) does not require one.
- Broadcast every successful change as `:nick!user@host MODE #chan +o bob`.

---

## 9. Testing — two layers, neither optional

**Inner loop (fast, `make test`).** Constructed-string unit tests. Proves the
logic is internally consistent. Run it constantly.

**Outer loop (slow, `nc` and irssi).** Proves protocol conformance: numerics,
prefixes, trailing params, CRLF, the JOIN sequence.

**The outer loop is versioned, not typed from memory.** Phase 2 decision: it
lives in `tests/it/*.sh`, outside the graded build, one script per concern.
Each starts its own `ircserv` on a spare port, drives it over TCP and asserts
on the bytes that come back. Two of them run the server under `valgrind`,
because the bug they cover — a freed `Client` still pointed at by a `Channel`,
and a `QUIT` sharing a packet with the next command — is silent without it.
`docs/README.md` lists them and how to run them.

> Two of those scripts were **validated by mutation**: the defect they exist to
> catch was deliberately reintroduced, and each was confirmed to fail. A test
> that has never seen its own bug is a guess.

> A parser can pass every test we write for it and still disagree with irssi.
> Unit tests prove we are consistent, not that we are correct. Only the RFC and
> the real client prove correctness.

Partial-packet test required by the subject:

```sh
nc -C 127.0.0.1 6667
com<Ctrl+D>man<Ctrl+D>d<Enter>
```

The server must see one command, `command`, not three fragments.

---

## 10. Working with AI on this project

Use it to explain concepts, review code we wrote, and generate throwaway test
scripts. Do not paste in generated files we have not read: the evaluation
requires either of us to explain any line, and "the AI wrote it" is a failed
defense.

Be actively skeptical on exactly two things:

1. **Numerics.** Models confidently invent codes and reword replies. Check
   every one against the table above and against irssi.
2. **C++98.** Models drift into C++11 constantly — `auto`, `nullptr`,
   `to_string`, range-for, brace init. The compiler catches it; read the
   suggestion before you accept it.

---

## 11. Limits and resource bounds

These are **not** Phase 4 polish. Two of them are protocol invariants and the
rest are the never-crash rule — the subject requires the server not to crash
"even when it runs out of memory", and an unbounded buffer is a memory
exhaustion bug reachable by one client that looks perfectly well behaved.
They also belong to the *design* of the units they live in: adding a cap later
changes a unit's contract and invalidates its Phase 1 tests.

Constants live in `include/Limits.hpp`.

| Constant | Value | Enforced in |
|---|---|---|
| `MAX_MESSAGE_LEN` | 512, CRLF included | protocol invariant, RFC 2812 §2.3 |
| `MAX_PAYLOAD_LEN` | 510 | `Server::sendToClient`, `Client::extractCommand` |
| `MAX_READ_BUFFER` | 4096 | `Client::appendToReadBuffer` |
| `MAX_OUTPUT_QUEUE` | 65536 | `Client::queueOutput` |
| `RECV_CHUNK` | 4096 | the poll loop |
| `MAX_NICKNAME_LEN` | 30 | `utils::isValidNickname` — see section 5 |
| `MAX_CHANNEL_LEN` | 50 | `utils::isValidChannelName`, RFC 2812 §1.3 |

### Policies

**Incoming line longer than 510.** Truncate to 510 and process it. This is what
real servers do, and it keeps a fat line from becoming a disconnect.

**Read buffer over 4096 with no complete line in it.** That is one unterminated
flood, not legitimate pipelining — the qualifier matters, because several
pipelined commands can legitimately make the buffer large. Send
`ERROR :Request too long` and disconnect. `appendToReadBuffer` returns `false`
to signal this; **check the return value.**

**Output queue over 65536.** The client has stopped reading while traffic keeps
arriving (suspended with Ctrl+Z, or hostile). Other servers call this SendQ
exceeded. Disconnect. `queueOutput` returns `false` to signal it.

**Outgoing line longer than 510.** Truncate in `sendToClient`, before the CRLF
is appended.

> This last one is the subtle one, and checking only the incoming length does
> not catch it. A client sends `PRIVMSG #chan :<504 bytes>` — legal, under 512.
> The server relays it as `:nick!user@host PRIVMSG #chan :<504 bytes>` and the
> prefix pushes the result past 512. What makes this cheap to fix is that
> `sendToClient` is the single choke point every outbound byte passes through,
> so one truncation there covers every command.

**NUL and stray control bytes.** `std::string` stores NUL fine, but any code
path that reaches for `.c_str()` will silently truncate at it. Strip NUL, bare
`\r` and bare `\n` when extracting a line. irssi will never send one; `nc` and
an evaluator piping binary will.

### One syscall per readiness event

`poll()` is **level-triggered**: if data remains in the kernel buffer after one
`recv()`, the next `poll()` reports the fd readable again immediately. Nothing
is lost and nothing stalls.

So do **not** loop `recv()` until `EAGAIN`. That pattern is mandatory for
edge-triggered `epoll` (`EPOLLET`), which only notifies on transitions, and it
is merely a throughput optimisation here. For this project it is actively worse:
the subject says reading any fd "without using `poll()`" is a grade 0, and a
loop issuing five `recv()`s off one readiness event is defensible but invites a
question you then have to talk your way out of. One `recv()` per readiness
event is trivially defensible, and it is fairer — draining lets one flooding
client monopolise the loop.

`send()` does not want a drain loop either: one `send()` reports how many bytes
the kernel accepted, and `POLLOUT` stays armed while output remains queued.
Arm `POLLOUT` **only** when there is something to write, or `poll()` returns
immediately every iteration and the loop spins at 100% CPU.

Still handle `EAGAIN` gracefully wherever it can appear — level-triggered does
not make it impossible.
