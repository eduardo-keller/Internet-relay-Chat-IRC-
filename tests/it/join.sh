#!/bin/bash
#
# Integration tests for JOIN — phase 3, step 1.
#
#   ./tests/it/join.sh [port]
#
# tests/test_cmd_channel.cpp covers the same ground with objects on the stack.
# What only a socket can show is the SEQUENCE as it actually arrives — JOIN,
# then 331 or 332, then 353, then 366 — and the broadcast reaching a second,
# genuinely separate connection. Getting that order wrong is the usual reason a
# channel window opens empty in irssi.

PORT=${1:-6707}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LOG=$(mktemp)
OUTA=$(mktemp)
OUTB=$(mktemp)
PASSED=0
FAILED=0

cleanup()
{
	[ -n "$A" ] && kill -9 $(pgrep -P "$A") "$A" 2>/dev/null
	[ -n "$B" ] && kill -9 $(pgrep -P "$B") "$B" 2>/dev/null
	if [ -n "$SRV" ]; then
		kill -INT "$SRV" 2>/dev/null
		wait "$SRV" 2>/dev/null
	fi
	rm -f "$LOG" "$OUTA" "$OUTB"
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
echo "join (porta $PORT, pid $SRV)"

REG='PASS secret\r\nNICK alice\r\nUSER alice 0 * :Alice\r\n'

# 1. THE SEQUENCE, in the order irssi depends on. The numerics are extracted in
#    arrival order and compared as one string, so a reordering fails here even
#    though every individual line would still be present.
OUT=$(talk "$REG"'JOIN #room\r\n')
check "a entrada devolve JOIN, 331, 353 e 366 nessa ordem" \
	"JOIN 331 353 366" \
	"$(printf '%s\n' "$OUT" | sed -n -e 's/^:alice!alice@127.0.0.1 \(JOIN\) .*/\1/p' \
		-e 's/^:ircserv \(3[0-9][0-9]\) .*/\1/p' | tr '\n' ' ' | sed 's/ $//')"

check "o JOIN volta com o prefixo do proprio cliente" \
	"1" "$(printf '%s\n' "$OUT" | grep -c '^:alice!alice@127.0.0.1 JOIN #room$')"
check "o criador do canal aparece com @ no 353" \
	"1" "$(printf '%s\n' "$OUT" | grep -c '^:ircserv 353 alice = #room :@alice$')"
check "o 366 usa a grafia com barra (D9)" \
	"1" "$(printf '%s\n' "$OUT" | grep -c '^:ircserv 366 alice #room :End of /NAMES list$')"

# 2. D18 — THE LINE IRSSI SENDS BY ITSELF ON EVERY CONNECT, before it has even
#    finished CAP. Anything at all after the welcome burst here is a line in the
#    user's status window that the subject calls an error.
OUT=$(talk "$REG"'JOIN :\r\n')
check "JOIN com lista de canais vazia nao responde nada (D18)" \
	"001 002 003 004 " \
	"$(printf '%s\n' "$OUT" | sed -n 's/^:ircserv \([0-9]*\) .*/\1/p' | tr '\n' ' ')"

# 3. But a JOIN with no parameter at all is still an error — nobody's client
#    sends that, it is a typo at an nc prompt.
check "JOIN sem parametro nenhum -> 461" \
	":ircserv 461 alice JOIN :Not enough parameters" \
	"$(talk "$REG"'JOIN\r\n' | tail -1)"

check "canal com prefixo & -> 403" \
	":ircserv 403 alice &foo :No such channel" \
	"$(talk "$REG"'JOIN &foo\r\n' | tail -1)"

check "JOIN antes do registro -> 451" \
	":ircserv 451 * :You have not registered" \
	"$(talk 'JOIN #room\r\n')"

# 4. TWO REAL CONNECTIONS. The first holds its socket open; the second joins
#    the same channel and the first has to be told about it. This is the half
#    that a unit test cannot prove.
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'PASS secret\r\nNICK ana\r\nUSER ana 0 * :Ana\r\nJOIN #sala\r\n' >&3
	cat <&3 > "$OUTA" &
	sleep 30
) &
A=$!
disown
sleep 1

(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'PASS secret\r\nNICK beto\r\nUSER beto 0 * :Beto\r\nJOIN #Sala\r\n' >&3
	cat <&3 > "$OUTB" &
	sleep 30
) &
B=$!
disown
sleep 2

check "quem ja estava no canal recebe o JOIN do novato" \
	"1" "$(grep -c '^:beto!beto@127.0.0.1 JOIN #sala' "$OUTA")"

#    #Sala found #sala: decision D5, and the reply carries the ORIGINAL
#    spelling back, not the one beto typed.
check "o canal e o mesmo apesar da grafia (#Sala = #sala)" \
	"1" "$(grep -c ' 366 beto #sala :End of /NAMES list' "$OUTB")"

check "o novato ve os dois membros no 353" \
	"1" "$(grep -c ' 353 beto = #sala :.*ana' "$OUTB")"
check "e ve a ana como operadora, nao a si mesmo" \
	"1" "$(grep -c ' 353 beto = #sala :.*@ana' "$OUTB")"
check "o novato nao ganhou @" \
	"0" "$(grep -c ' 353 beto = #sala :.*@beto' "$OUTB")"

#    A repeated JOIN says nothing at all: no second sequence for beto, and no
#    second arrival announced to ana.
BEFORE=$(grep -c 'JOIN #sala' "$OUTA")
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'PASS secret\r\nNICK beto2\r\nUSER b 0 * :B\r\n' >&3
	sleep 1
) > /dev/null 2>&1
sleep 1
check "nenhum JOIN extra chegou a ana" \
	"$BEFORE" "$(grep -c 'JOIN #sala' "$OUTA")"

kill -0 "$SRV" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor continua vivo no fim" "vivo" "$ALIVE"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
