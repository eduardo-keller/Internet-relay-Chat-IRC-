#!/bin/bash
#
# Integration tests for MODE — phase 3, step 0.6 onwards.
#
#   ./tests/it/mode.sh [port]
#
# WHAT THIS FILE IS FOR, TODAY: the unprompted "MODE <nick> +i" that irssi
# 1.4.5 sends about two seconds after registering, on every connection, without
# joining anything. It was captured on the wire in step 0 of docs/FASE3.md.
#
# With no MODE handler the dispatcher answers 421 :Unknown command, which puts
# an error line in irssi's status window — and the subject requires the
# reference client to connect "without encountering any error". Answering 403
# :No such channel instead would be the same mistake wearing better clothes,
# which is exactly how decision D2 went wrong with CAP. The right answer to a
# user mode, from a server that implements none, is nothing at all.
#
# The channel side of MODE — the 324 query and the five flags — needs a channel
# to exist, and therefore JOIN. Those cases arrive here in steps 1 and 9 to 12.

PORT=${1:-6705}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LOG=$(mktemp)
PASSED=0
FAILED=0

cleanup()
{
	if [ -n "$SRV" ]; then
		kill -INT "$SRV" 2>/dev/null
		wait "$SRV" 2>/dev/null
	fi
	rm -f "$LOG"
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
echo "mode (porta $PORT, pid $SRV)"

REG='PASS secret\r\nNICK alice\r\nUSER alice 0 * :Alice\r\n'

# 1. THE IRSSI CASE, byte for byte: register, then send exactly what irssi
#    sends by itself. Everything after the four registration numerics must be
#    empty — no 421, and no 403 either.
OUT=$(talk "$REG"'MODE alice +i\r\n')
check "MODE num nick nao responde nada (o caso real do irssi)" \
	"001 002 003 004 " \
	"$(printf '%s\n' "$OUT" | sed -n 's/^:ircserv \([0-9]*\) .*/\1/p' | tr '\n' ' ')"

# 2. And specifically not the two numerics it would be tempting to send.
check "MODE num nick nao vira 421" \
	"0" "$(printf '%s\n' "$OUT" | grep -c ' 421 ')"
check "MODE num nick nao vira 403" \
	"0" "$(printf '%s\n' "$OUT" | grep -c ' 403 ')"

# 3. A user-mode QUERY, with no flags, is just as silent.
check "MODE alice (consulta de modo de usuario) tambem e silencioso" \
	"001 002 003 004 " \
	"$(talk "$REG"'MODE alice\r\n' | sed -n 's/^:ircserv \([0-9]*\) .*/\1/p' | tr '\n' ' ')"

# 4. A channel that does not exist IS an error, and a different one from
#    "unknown command".
check "MODE num canal inexistente -> 403" \
	":ircserv 403 alice #nope :No such channel" \
	"$(talk "$REG"'MODE #nope\r\n' | tail -1)"

# 5. No parameter at all is 461.
check "MODE sem parametro -> 461" \
	":ircserv 461 alice MODE :Not enough parameters" \
	"$(talk "$REG"'MODE\r\n' | tail -1)"

# 6. MODE is behind the registration gate — it is not one of the six commands
#    allowed before PASS/NICK/USER.
check "MODE antes do registro -> 451" \
	":ircserv 451 * :You have not registered" \
	"$(talk 'MODE #chan\r\n')"

kill -0 "$SRV" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor continua vivo no fim" "vivo" "$ALIVE"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
