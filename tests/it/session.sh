#!/bin/bash
#
# Integration tests for the session commands — phase 2, step 7.
#
#   ./tests/it/session.sh [port]
#
# The case that matters here is a QUIT sharing a TCP packet with the command
# after it. cmdQuit is the first handler that marks a client for disconnection,
# so this is the first time the drain loop's isDisconnecting() guard is
# exercised at all.
#
# TWO SEPARATE MECHANISMS PROTECT THAT PACKET, and it is worth being precise
# about which does what — this was verified by deliberately removing the guard
# and re-running:
#
#   * The DEFERRED DELETE (disconnectClient marks, reapDisconnected deletes at
#     the end of the poll iteration) is what makes it memory-safe. With it in
#     place, removing the guard produces NO valgrind error at all: the Client
#     is still perfectly alive for the rest of the drain loop.
#   * The DRAIN GUARD is what makes it correct. Without it the server happily
#     dispatches the line after the QUIT — a PRIVMSG delivered by someone who
#     has already left, or a JOIN from a client about to be swept out of every
#     channel. The mutation shows up as a 421 that should never have existed.
#
# So the assertion below is behavioural, and valgrind is here to prove the
# other half: that the reap frees everything exactly once, with the client's
# ERROR flushed on the way out.

PORT=${1:-6701}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LOG=$(mktemp)
PASSED=0
FAILED=0

cleanup()
{
	[ -n "$VG" ] && kill -9 "$VG" 2>/dev/null
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
	while IFS= read -r -t 2 l <&3 2>/dev/null; do printf '%s\n' "$l" | tr -d '\r'; done
}

if ! command -v valgrind > /dev/null; then
	echo "valgrind nao instalado — este teste depende dele"
	exit 1
fi

valgrind --leak-check=full --error-exitcode=42 "$ROOT/ircserv" "$PORT" secret \
	> "$LOG" 2>&1 &
VG=$!
sleep 3
if ! kill -0 "$VG" 2>/dev/null; then
	echo "servidor nao subiu na porta $PORT:"
	cat "$LOG"
	exit 1
fi
echo "session (porta $PORT, pid $VG, sob valgrind)"

# 1. PING comes back with the same token. irssi pings on a timer and reports a
#    timeout if this is wrong.
check "PING devolve o mesmo token" \
	":ircserv PONG ircserv :token123" "$(talk 'PING token123\r\n')"

# 2. A token with spaces survives as a trailing parameter.
check "token com espacos volta inteiro" \
	":ircserv PONG ircserv :two words" "$(talk 'PING :two words\r\n')"

REG3='PASS secret\r\nNICK alice\r\nUSER alice 0 * :Alice\r\n'

# 3. CAP IS ANSWERED WITH AN EMPTY CAPABILITY LIST (decision D17, which
#    supersedes D2). This assertion used to say the opposite — "no reply and no
#    error" — and it was green for the whole of phase 2. The first time irssi
#    1.4.5 was pointed at the server it printed "Waiting for CAP LS response..."
#    and never sent PASS, NICK or USER: silence is not a non-answer to a real
#    client, it is a stall, and nobody could register at all.
check "CAP LS 302 devolve a lista de capacidades vazia" \
	":ircserv CAP * LS :" "$(talk 'CAP LS 302\r\n')"

check "CAP REQ e recusado com NAK" \
	":ircserv CAP * NAK :multi-prefix" "$(talk 'CAP REQ :multi-prefix\r\n')"

check "CAP END nao responde nada" "" "$(talk 'CAP END\r\n')"

# 3.1 THE REGRESSION THAT THE BUG ITSELF WOULD HAVE CAUGHT: the exact opening
#     irssi sends, all in one packet. The CAP answer AND the four registration
#     numerics have to come back. With the old silent handler this still
#     "worked" over nc — because nc does not wait for the CAP reply the way a
#     real client does — which is precisely why the unit tests never noticed.
OUT=$(talk 'CAP LS 302\r\nCAP END\r\nPASS secret\r\nNICK carol\r\nUSER carol 0 * :Carol\r\n')
check "abertura completa do irssi num pacote so: responde o CAP" \
	"1" "$(printf '%s\n' "$OUT" | grep -c '^:ircserv CAP \* LS :$')"
check "abertura completa do irssi num pacote so: registra" \
	"001 002 003 004 " \
	"$(printf '%s\n' "$OUT" | sed -n 's/^:ircserv \([0-9]*\) .*/\1/p' | tr '\n' ' ')"

# 3.2 WHO and WHOIS are accepted and ignored, for the same reason as CAP: irssi
#     sends both by itself once a channel has two people in it, and 421 in the
#     status window is an error the subject forbids. Step 1.5 of FASE3.md.
OUT=$(talk "$REG3"'WHO #test\r\nWHOIS alice\r\n')
check "WHO e WHOIS nao respondem nada" \
	"001 002 003 004 " \
	"$(printf '%s\n' "$OUT" | sed -n 's/^:ircserv \([0-9]*\) .*/\1/p' | tr '\n' ' ')"
check "e especificamente nao viram 421" \
	"0" "$(printf '%s\n' "$OUT" | grep -c ' 421 ')"

# 4. THE USE-AFTER-FREE CASE. Everything below arrives in ONE write: register,
#    quit, and then a command that must never be dispatched.
OUT=$(talk 'PASS secret\r\nNICK alice\r\nUSER alice 0 * :Alice\r\nQUIT :bye\r\nFOO\r\n')

check "o registro completo ainda aconteceu" \
	"1" "$(printf '%s\n' "$OUT" | grep -c ' 001 alice ')"
check "o QUIT foi atendido" \
	"1" "$(printf '%s\n' "$OUT" | grep -c '^ERROR :bye$')"
# The whole point: the line after QUIT is never processed. A 421 here would
# mean the drain loop kept going on a client already marked for death.
check "a linha depois do QUIT NAO foi despachada" \
	"0" "$(printf '%s\n' "$OUT" | grep -c '421')"

# 5. The server survived all of it, and valgrind has nothing to report.
kill -0 "$VG" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor continua vivo" "vivo" "$ALIVE"

kill -INT "$VG"
wait "$VG"
VGEXIT=$?
VG=
check "valgrind sem erros (42 = erro detectado)" "0" "$VGEXIT"
check "nenhuma leitura ou escrita em memoria liberada" \
	"0" "$(grep -c 'Invalid read\|Invalid write' "$LOG")"
check "nada definitivamente vazado" \
	"0" "$(grep -c 'definitely lost: [^0]' "$LOG")"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
