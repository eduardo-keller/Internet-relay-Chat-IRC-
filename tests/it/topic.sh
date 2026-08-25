#!/bin/bash
#
# Integration tests for TOPIC — phase 3, step 6.
#
#   ./tests/it/topic.sh [port]
#
# TOPIC both reads and writes depending on its parameters, and the pair that
# only a socket can tell apart is "TOPIC #c" against "TOPIC #c :" — one asks,
# the other erases. They differ by a single byte on the wire, and an
# implementation that dropped the empty trailing parameter would collapse them
# into one.
#
# THE +t PATH IS NOT REACHABLE FROM HERE YET, for the same reason the JOIN
# gates are not (see tests/it/join.sh): over a socket the only way to put a
# mode on a channel is MODE, and cmdMode still refuses every change until steps
# 9 to 12. The 482 case is covered in tests/test_cmd_channel.cpp, where the
# Channel API sets +t directly, and comes back here when MODE can set it.

PORT=${1:-6713}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LOG=$(mktemp)
OUTA=$(mktemp)
PASSED=0
FAILED=0

cleanup()
{
	[ -n "$A" ] && kill -9 $(pgrep -P "$A") "$A" 2>/dev/null
	if [ -n "$SRV" ]; then
		kill -INT "$SRV" 2>/dev/null
		wait "$SRV" 2>/dev/null
	fi
	rm -f "$LOG" "$OUTA"
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

# CR stripped on READ, not on write: tr block-buffers into a file and the
# assertions would read an empty one. See tests/it/privmsg.sh.
seen()
{
	tr -d '\r' < "$1"
}

# Body in a subshell so the socket dies with the call — see tests/it/part.sh.
talk()
{
	(
		exec 3<>/dev/tcp/127.0.0.1/"$PORT"
		printf "$1" >&3
		while IFS= read -r -t 1 l <&3 2>/dev/null; do
			printf '%s\n' "$l" | tr -d '\r'
		done
	)
}

"$ROOT/ircserv" "$PORT" secret > "$LOG" 2>&1 &
SRV=$!
sleep 0.5
if ! kill -0 "$SRV" 2>/dev/null; then
	echo "servidor nao subiu na porta $PORT:"
	cat "$LOG"
	exit 1
fi
echo "topic (porta $PORT, pid $SRV)"

REG='PASS secret\r\nNICK zeca\r\nUSER zeca 0 * :Zeca\r\n'

# 1. The errors.
check "TOPIC sem parametro -> 461" \
	":ircserv 461 zeca TOPIC :Not enough parameters" \
	"$(talk "$REG"'TOPIC\r\n' | tail -1)"

check "TOPIC de canal inexistente -> 403" \
	":ircserv 403 zeca #nope :No such channel" \
	"$(talk "$REG"'TOPIC #nope\r\n' | tail -1)"

check "TOPIC antes do registro -> 451" \
	":ircserv 451 * :You have not registered" \
	"$(talk 'TOPIC #x\r\n')"

# 2. Query and change, in one connection: ask (331), set, ask again (332).
OUT=$(talk "$REG"'JOIN #sala\r\nTOPIC #sala\r\nTOPIC #sala :o assunto do dia\r\nTOPIC #sala\r\n')
#    TWO of them, not one: the JOIN sequence itself already answers 331 for a
#    channel with no topic, and the explicit TOPIC query then answers it again.
#    Expecting one here was wrong about our own JOIN, not about TOPIC.
check "canal sem topico responde 331 (no JOIN e na consulta)" \
	"2" "$(printf '%s\n' "$OUT" | grep -c '^:ircserv 331 zeca #sala :No topic is set$')"
check "a alteracao volta como TOPIC, com o prefixo de quem mudou" \
	"1" "$(printf '%s\n' "$OUT" | grep -c '^:zeca!zeca@127.0.0.1 TOPIC #sala :o assunto do dia$')"
check "e a consulta seguinte responde 332 com o texto" \
	"1" "$(printf '%s\n' "$OUT" | grep -c '^:ircserv 332 zeca #sala :o assunto do dia$')"

# 3. THE ONE-BYTE DIFFERENCE. "TOPIC #sala" asks; "TOPIC #sala :" erases.
OUT=$(talk "$REG"'JOIN #limpo\r\nTOPIC #limpo :vai sumir\r\nTOPIC #limpo :\r\nTOPIC #limpo\r\n')
check "trailing vazio limpa o topico" \
	"1" "$(printf '%s\n' "$OUT" | grep -c '^:zeca!zeca@127.0.0.1 TOPIC #limpo :$')"
#    Again two: one from the JOIN that opened the channel, one from the query
#    after the topic was erased. The second is the one this case is about.
check "e a consulta seguinte volta a 331" \
	"2" "$(printf '%s\n' "$OUT" | grep -c '^:ircserv 331 zeca #limpo :No topic is set$')"

# 4. A LIVE WITNESS: the topic is a broadcast, not a private answer.
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'PASS secret\r\nNICK ana\r\nUSER ana 0 * :Ana\r\nJOIN #sala\r\n' >&3
	cat <&3 > "$OUTA" &
	sleep 30
) &
A=$!
disown
sleep 2

talk 'PASS secret\r\nNICK beto\r\nUSER beto 0 * :Beto\r\nJOIN #sala\r\nTOPIC #sala :beto mudou\r\n' > /dev/null
sleep 1
check "quem estava no canal recebe a mudanca de topico" \
	"1" "$(seen "$OUTA" | grep -c '^:beto!beto@127.0.0.1 TOPIC #sala :beto mudou$')"

#    A newcomer is told the current topic on the way in — 332 instead of 331.
check "quem entra depois recebe o topico atual no 332" \
	"1" "$(talk "$REG"'JOIN #sala\r\n' | grep -c '^:ircserv 332 zeca #sala :beto mudou$')"

# 5. Membership is required, even to read.
check "TOPIC de quem nao esta no canal -> 442" \
	":ircserv 442 zeca #sala :You're not on that channel" \
	"$(talk "$REG"'TOPIC #sala\r\n' | tail -1)"

kill -0 "$SRV" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor continua vivo no fim" "vivo" "$ALIVE"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
