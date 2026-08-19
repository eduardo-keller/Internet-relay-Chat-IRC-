#!/bin/bash
#
# Hardening tests — phase 2, step 8.
#
#   ./tests/it/hardening.sh [port]
#
# The other scripts drive the server the way a client is supposed to. This one
# does not: it sends binary garbage, NUL bytes, oversized lines and fifty
# connections at once. The subject's rule is absolute — the server must never
# crash or quit unexpectedly, "even when it runs out of memory" — so every case
# here is really the same assertion twice: the reply is sane, AND the process
# is still standing afterwards.

PORT=${1:-6703}
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

alive()
{
	kill -0 "$SRV" 2>/dev/null && echo vivo || echo MORTO
}

"$ROOT/ircserv" "$PORT" secret > "$LOG" 2>&1 &
SRV=$!
sleep 0.5
if ! kill -0 "$SRV" 2>/dev/null; then
	echo "servidor nao subiu na porta $PORT:"
	cat "$LOG"
	exit 1
fi
echo "hardening (porta $PORT, pid $SRV)"

# 1. 200 bytes straight from /dev/urandom. No line structure, no valid UTF-8,
#    possibly NUL bytes and stray CRs in the middle.
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	head -c 200 /dev/urandom >&3
	sleep 0.5
) > /dev/null 2>&1
sleep 0.5
check "200 bytes de /dev/urandom nao derrubam" "vivo" "$(alive)"

# 2. Five bursts of it, back to back.
for i in 1 2 3 4 5; do
	(
		exec 3<>/dev/tcp/127.0.0.1/"$PORT"
		head -c 1000 /dev/urandom >&3
		sleep 0.2
	) > /dev/null 2>&1
done
check "cinco rajadas de lixo binario nao derrubam" "vivo" "$(alive)"

# 3. A NUL in the middle of a command. std::string holds it fine, but anything
#    reaching for .c_str() truncates there — so it is stripped when the line is
#    extracted, and the command must still be recognised.
OUT=$(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'PI\000NG token\r\n' >&3
	IFS= read -r -t 2 l <&3 2>/dev/null
	printf '%s' "$l" | tr -d '\r'
)
check "NUL no meio do comando e descartado, o comando funciona" \
	":ircserv PONG ircserv :token" "$OUT"

# 4. A line far over the protocol limit but still INSIDE the read buffer is
#    truncated to 510 and processed — not a disconnect.
#
#    The boundary is MAX_READ_BUFFER, not 510. Past 4096 bytes with no
#    terminator yet in the buffer, the server cannot know whether a CRLF is
#    ever coming and refuses the flood instead of growing without bound
#    (read_path.sh covers that side). A legitimate client never gets near
#    either number: RFC 2812 caps a message at 512.
OUT=$(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf '%0.sA' $(seq 1 4000) >&3
	printf '\r\n' >&3
	IFS= read -r -t 2 l <&3 2>/dev/null
	printf '%s' "$l" | tr -d '\r' | wc -c
)
check "linha de 4000 bytes (dentro do buffer) sai truncada em 510" "510" "$OUT"

# 5. Fifty connections at once, each registering.
CLIENTS=""
for i in $(seq 1 50); do
	(
		exec 3<>/dev/tcp/127.0.0.1/"$PORT"
		printf 'PASS secret\r\nNICK user%d\r\nUSER u%d 0 * :U\r\n' "$i" "$i" >&3
		sleep 3
	) > /dev/null 2>&1 &
	CLIENTS="$CLIENTS $!"
done
sleep 2
# 4 baseline fds (stdin, stdout, stderr, listening socket) plus one per client.
FDS=$(ls /proc/"$SRV"/fd 2>/dev/null | wc -l)
check "50 conexoes simultaneas ficam todas abertas" "54" "$FDS"
check "servidor vivo com 50 clientes" "vivo" "$(alive)"
# Waiting on the collected pids, NOT a bare `wait`: the server is a background
# job of this script too, and a bare wait would block on it forever.
wait $CLIENTS 2>/dev/null
sleep 1
check "os fds voltam ao normal depois que todos saem" "4" \
	"$(ls /proc/"$SRV"/fd 2>/dev/null | wc -l)"

# 6. Connect and close immediately, repeatedly: no registration, no data, just
#    churn. This is what an evaluator's Ctrl+C in a loop looks like.
for i in $(seq 1 30); do
	(exec 3<>/dev/tcp/127.0.0.1/"$PORT") > /dev/null 2>&1
done
sleep 1
check "30 conexoes abertas e fechadas na hora" "vivo" "$(alive)"
check "sem vazar fd nesse churn" "4" "$(ls /proc/"$SRV"/fd 2>/dev/null | wc -l)"

# 7. A client that sends only a CR, only an LF, or only spaces.
for payload in '\r\n' '\n' '   \r\n' ':\r\n' ': \r\n'; do
	(
		exec 3<>/dev/tcp/127.0.0.1/"$PORT"
		printf "$payload" >&3
		sleep 0.2
	) > /dev/null 2>&1
done
check "linhas degeneradas (so CR, so LF, so espacos, so prefixo)" \
	"vivo" "$(alive)"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
