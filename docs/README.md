# ft_irc

An IRC server written in C++98 for 42 School. Mandatory part only — no bonus.

Reference client: **irssi**.

## Build

```sh
make            # builds ./ircserv
make test       # builds and runs the unit tests
make clean      # removes object files
make fclean     # removes object files and binaries
make re         # fclean + all
```

Compiled with `c++ -Wall -Wextra -Werror -std=c++98`.

## Run

```sh
./ircserv <port> <password>
```

- `port` — TCP port to listen on (1024–65535)
- `password` — the password every client must send with `PASS`

```sh
./ircserv 6667 secret
```

Connect with irssi:

```sh
irssi
/connect localhost 6667 secret
/join #test
```

Or with netcat, for raw protocol work:

```sh
nc -C 127.0.0.1 6667
PASS secret
NICK alice
USER alice 0 * :Alice
JOIN #test
PRIVMSG #test :hello
```

## Testing

### Two binaries, two `main()` functions

A C++ program can have exactly one `main()`, and this repo has two files that
define one. The Makefile keeps them apart:

| Binary | Built from | Entry point |
|---|---|---|
| `ircserv` | `src/*.cpp` | `main()` in `src/main.cpp` |
| `run_tests` | `tests/*.cpp` + `src/*.cpp` **minus `main.o`** | `main()` in `tests/test_main.cpp` |

The exclusion is this line in the Makefile:

```make
LIB_OBJS := $(filter-out $(OBJ_DIR)/main.o, $(OBJS))
```

Without it the linker sees two `main()`s and fails with
`multiple definition of main`. **Consequence: anything written inside
`main.cpp` is unreachable from the tests.** Real logic goes in `Server.cpp`,
`Channel.cpp`, `Utils.cpp` — never in `main.cpp`.

### What each entry point does

`src/main.cpp` validates the command line and exits. Three checks, then a
placeholder message. It will be replaced in Phase 2 by
`Server server(port, password); server.run();`.

Its seven `#include`s are deliberate: nothing calls into them, but they force
the compiler to parse every contract header on every build, so a syntax error
or a C++11 slip in someone else's header breaks `make` immediately instead of
three days later. It still links, because declarations without definitions only
fail at link time *if something calls them*.

`tests/test_main.cpp` is a homemade harness — no framework. `check(bool, name)`
and `checkEqual(actual, expected, name)` bump a pass/fail counter and print one
line each; `checkEqual` prints both strings on failure so the difference is
visible. `main()` returns 0 if everything passed and 1 otherwise, which is what
makes `make test` usable as a gate.

### Exercising `ircserv`

`echo $?` prints the exit status of the previous command: 0 means success,
non-zero means failure. Evaluators check this.

| Command | Output | Exit |
|---|---|---|
| `./ircserv` | `Usage: ./ircserv <port> <password>` | 1 |
| `./ircserv 6667` | `Usage: ...` | 1 |
| `./ircserv 6667 secret extra` | `Usage: ...` | 1 |
| `./ircserv abc secret` | `Error: port must be a number between 1024 and 65535` | 1 |
| `./ircserv 80 secret` | `Error: port must be ...` | 1 |
| `./ircserv 99999 secret` | `Error: port must be ...` | 1 |
| `./ircserv 6667 ""` | `Error: password must not be empty` | 1 |
| `./ircserv 6667 secret` | `ircserv: would listen on port 6667 (not implemented yet)` | 0 |

All at once:

```sh
for a in "" "6667" "abc secret" "80 secret" "99999 secret" "6667 secret"; do
  printf '%-26s -> ' "ircserv $a"
  eval "./ircserv $a" >/dev/null 2>&1
  echo "exit=$?"
done
```

### Running the unit tests

```sh
make test        # build and run in one step
```

When iterating, split it so you are not rebuilding to re-run:

```sh
make run_tests   # build only
./run_tests      # run only
echo $?          # 0 = all green
```

### Adding a test

A test can only call a function that has a **definition** in `src/`.
`include/Utils.hpp` currently declares `utils::toIrcLower` but no
`src/Utils.cpp` defines it, so a test calling it fails at *link* time with
`undefined reference to utils::toIrcLower`. That is not a broken Makefile — it
is the missing file. Always write the `src/*.cpp` first, then the test.

```cpp
// in tests/test_main.cpp, inside main()
checkEqual(utils::toIrcLower("Nick[42]"), "nick{42}", "casemapping [] -> {}");
check(utils::isValidNickname("alice"),  "alice is valid");
check(!utils::isValidNickname("4lice"), "nick cannot start with a digit");
```

New files such as `tests/test_parser.cpp` are picked up automatically —
`tests/*.cpp` is globbed, so no Makefile edit is needed. Do not put a second
`main()` in them.

### The integration tests

`make test` proves the logic is internally consistent. It cannot prove that the
server speaks IRC over a socket, because nothing in it opens one. That is what
`tests/it/*.sh` is for: each script starts its own `ircserv` on a spare port,
drives it over TCP, asserts on the bytes that come back, and shuts it down.

They are **not** part of the graded build and `make` never runs them. Run them
by hand, with the server stopped — each script starts its own:

```sh
./tests/it/read_path.sh      # packet reassembly: the subject's com^Dman^Dd case
./tests/it/write_path.sh     # CRLF, the 510-byte truncation, ERROR before close
./tests/it/dispatch.sh       # 421, 451, case-insensitivity, blank lines
./tests/it/registration.sh   # PASS/NICK/USER, 001-004, 433 with two connections
./tests/it/session.sh        # PING/PONG, QUIT glued to another command, CAP
./tests/it/hardening.sh      # binary garbage, NUL bytes, 50 connections at once
./tests/it/mode.sh           # the unprompted MODE irssi sends on every connect
./tests/it/channel_seam.sh   # the disconnect sweep, under valgrind
./tests/it/join.sh           # the JOIN sequence, and two clients in one channel
./tests/it/part.sh           # leaving, and the channel lifecycle around it
./tests/it/privmsg.sh        # channel and private messages, and the 510 cut
./tests/it/topic.sh          # reading, setting and clearing the topic
./tests/it/kick.sh           # the first operator command, over two live clients
./tests/it/invite.sh         # invitations, and the RFC 1459 order of 341
./tests/it/full_session.sh   # the whole surface in one server lifetime, under valgrind

for t in tests/it/*.sh; do "$t" || echo "FAILED: $t"; done
```

Each takes a port as its optional first argument, defaulting to one in the
6690-6704 range so that two of them never collide. Each prints one line per
case and exits non-zero if anything failed, so they work in a loop like the one
above.

Three of them run the server under **valgrind** rather than plain, because the
bugs they cover are invisible otherwise: `channel_seam.sh` (a `Client` deleted
while a `Channel` still points at it), `session.sh` (a `QUIT` sharing a packet
with the command after it) and `full_session.sh` (the whole surface at once —
channels, invitations, operator sets, a `kill -9` in the middle of all three).
They need `valgrind` installed and take a few seconds longer.

`channel_seam.sh` used to **skip itself** while `JOIN` was not in the command
table. It runs for real since phase 3 step 1, and its clients now complete
PASS/NICK/USER before joining, because JOIN sits behind the registration gate.

> These scripts assert on what the server sends back. Where an earlier version
> asserted on the server's own log output, that was a stopgap for the days
> before the server could reply at all; a debug line we write ourselves proves
> much less than the protocol bytes a client actually receives.

## Layout

`src/` holds the implementation and `include/` the headers, one class per file.
The design has a single organising rule: **logic never touches file
descriptors**. `Message`, `Channel`, the validation helpers in `Utils`, and the
buffering inside `Client` all operate on strings and objects, which makes them
testable without a socket — those tests live in `tests/` and run via
`make test`. Only `Server` does real I/O: it owns the listening socket, the
single `poll()` loop over every fd, and the client and channel registries.
Command handlers sit in between: they receive a parsed `Message` and reach the
network only through a small, deliberately minimal set of `Server` methods.

The contract between those pieces — module ownership, the handler signature,
and every reply numeric we emit — is documented in
[ARCHITECTURE.md](ARCHITECTURE.md). Planning and task tracking are in
[PLANO.md](PLANO.md) and [TASKS.md](TASKS.md).
