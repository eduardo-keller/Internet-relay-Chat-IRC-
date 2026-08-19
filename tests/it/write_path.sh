#!/bin/bash
#
# Integration tests for the WRITE path — the outbound contract.
#
#   ./tests/it/write_path.sh [port]
#
# read_path.sh proves the server rebuilt the right commands. This one proves
# the bytes it sends back obey the protocol: CRLF on every line, nothing longer
# than 512 on the wire, and queued output actually reaching a client that is
# being disconnected.

PORT=${1:-6692}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LOG=$(mktemp)
PASSED=0
FAILED=0

cleanup()
{
	[ -n "$IDLE" ] && kill -9 "$IDLE" 2>/dev/null
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

"$ROOT/ircserv" "$PORT" secret > "$LOG" 2>&1 &
SRV=$!
sleep 0.5
if ! kill -0 "$SRV" 2>/dev/null; then
	echo "servidor nao subiu na porta $PORT:"
	cat "$LOG"
	exit 1
fi
echo "write path (porta $PORT, pid $SRV)"

# 1. Every line ends in CRLF, checked byte by byte. `read` strips the LF; the
#    CR it leaves behind is the proof. A server that ends lines with a bare LF
#    works with nc and fails against strict clients.
LAST=$(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'X\r\n' >&3
	IFS= read -r -t 2 l <&3
	printf '%s' "$l" | od -c | head -3 | tr -s ' ' | grep -o '\\r' | head -1
)
check "a resposta termina em CR antes do LF" '\r' "$LAST"

# 2. An outgoing line longer than the limit is truncated to 510, so that with
#    CRLF it is exactly the 512 of RFC 2812. Here the server builds the long
#    line itself: ":ircserv 421 <510 chars> :Unknown command" is well past the
#    limit even though what the client sent was legal.
SIZE=$(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf '%0.sa' $(seq 1 600) >&3
	printf '\r\n' >&3
	IFS= read -r -t 2 l <&3
	printf '%s' "$l" | tr -d '\r' | wc -c
)
check "linha de saida longa sai truncada em 510" "510" "$SIZE"

# 3. Queued output reaches a client that is on its way out. The ERROR is queued
#    on an ALREADY MARKED client, so without the best-effort flush in
#    reapDisconnected it would be closed with the message still in the queue.
ERR=$(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf '%0.sx' $(seq 1 5000) >&3
	IFS= read -r -t 2 l <&3
	printf '%s' "$l" | tr -d '\r'
)
check "ERROR chega antes de o socket fechar" "ERROR :Request too long" "$ERR"

# 4. POLLOUT must be armed only when there is something to write. Armed
#    unconditionally it is always ready, poll() returns instantly every
#    iteration, and this is where that shows up.
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'KEEPALIVE\r\n' >&3
	sleep 6
) &
IDLE=$!
sleep 1
A=$(awk '{print $14+$15}' /proc/"$SRV"/stat)
sleep 3
B=$(awk '{print $14+$15}' /proc/"$SRV"/stat)
check "CPU ociosa com cliente conectado e fila vazia" "0" "$((B - A))"
kill -9 "$IDLE" 2>/dev/null
IDLE=

kill -0 "$SRV" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor continua vivo no fim" "vivo" "$ALIVE"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
