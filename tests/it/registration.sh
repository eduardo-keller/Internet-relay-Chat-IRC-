#!/bin/bash
#
# Integration tests for registration — phase 2, step 6.
#
#   ./tests/it/registration.sh [port]
#
# tests/test_cmd_registration.cpp covers the state machine with objects on the
# stack. What it CANNOT reach honestly is 433: a duplicate nickname needs two
# clients inside Server::_clients, and clients only get there through accept().
# Rather than open a back door in the seam, that case is proved here with two
# live connections — which is better evidence anyway.

PORT=${1:-6698}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LOG=$(mktemp)
PASSED=0
FAILED=0

cleanup()
{
	[ -n "$HOLDER" ] && kill -9 "$HOLDER" 2>/dev/null
	if [ -n "$SRV" ]; then
		kill -INT "$SRV" 2>/dev/null
		wait "$SRV" 2>/dev/null
	fi
	rm -f "$LOG" "$HOLDLOG"
}
trap cleanup EXIT

check()
{
	if [ "$2" = "$3" ]; then
		echo "  ok   $1"
		PASSED=$((PASSED + 1))
	else
		echo "  FAIL $1"
		echo "         esperado: [$2]"
		echo "         obtido:   [$3]"
		FAILED=$((FAILED + 1))
	fi
}

talk()
{
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf "$1" >&3
	# stderr is dropped because a server that closes on us — the 464 case —
	# makes read report the reset, which is the expected end of that test.
	while IFS= read -r -t 1 l <&3 2>/dev/null; do printf '%s\n' "$l" | tr -d '\r'; done
}

"$ROOT/ircserv" "$PORT" secret > "$LOG" 2>&1 &
SRV=$!
sleep 0.5
if ! kill -0 "$SRV" 2>/dev/null; then
	echo "servidor nao subiu na porta $PORT:"
	cat "$LOG"
	exit 1
fi
echo "registration (porta $PORT, pid $SRV)"

# 1. The whole burst, from a single packet carrying all three commands — which
#    is exactly how irssi sends them.
OUT=$(talk 'PASS secret\r\nNICK alice\r\nUSER alice 0 * :Alice Liddell\r\n')
CODES=$(printf '%s\n' "$OUT" | sed -n 's/^:ircserv \([0-9]*\) .*/\1/p' | tr '\n' ' ')
check "registro completo devolve 001 002 003 004 nessa ordem" \
	"001 002 003 004 " "$CODES"

FIRST=$(printf '%s\n' "$OUT" | head -1)
check "001 traz o prefixo completo" \
	":ircserv 001 alice :Welcome to the Internet Relay Network alice!alice@127.0.0.1" \
	"$FIRST"

# 2. 433 needs a nickname that is genuinely taken, so this connection is held
#    open in the background for the rest of the test.
HOLDLOG=$(mktemp)
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'PASS secret\r\nNICK alice\r\nUSER alice 0 * :Alice\r\n' >&3
	cat <&3 > "$HOLDLOG" &
	sleep 30
) &
HOLDER=$!
sleep 1

#    ALICE, not alice: collisions are case-insensitive (RFC 2812 section 2.2),
#    and getting that wrong lets two people answer to the same name.
OUT=$(talk 'PASS secret\r\nNICK ALICE\r\nUSER bob 0 * :Bob\r\n')
check "nick duplicado, insensivel a maiusculas -> 433" \
	":ircserv 433 * ALICE :Nickname is already in use" \
	"$(printf '%s\n' "$OUT" | head -1)"

#    And the rejected client is NOT registered: no burst followed.
check "quem levou 433 nao recebe a rajada de boas-vindas" \
	"0" "$(printf '%s\n' "$OUT" | grep -c ' 001 ')"

# 3. A free nickname still works while alice is connected.
OUT=$(talk 'PASS secret\r\nNICK carol\r\nUSER carol 0 * :Carol\r\n')
check "outro nick livre registra normalmente" \
	"1" "$(printf '%s\n' "$OUT" | grep -c ' 001 carol ')"

# 4. A wrong password is 464 and the connection goes away. The ERROR arriving
#    after it is the deferred disconnect flushing its queue before close().
OUT=$(talk 'PASS wrongpassword\r\nNICK dave\r\n')
check "senha errada -> 464 seguido de ERROR" \
	":ircserv 464 * :Password incorrect
ERROR :Password incorrect" "$OUT"

# 5. Commands that need registration are still refused for a client that only
#    got as far as PASS.
check "PONG antes de completar o registro -> 451" \
	":ircserv 451 * :You have not registered" \
	"$(talk 'PASS secret\r\nPONG token\r\n')"

kill -0 "$SRV" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor continua vivo no fim" "vivo" "$ALIVE"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
