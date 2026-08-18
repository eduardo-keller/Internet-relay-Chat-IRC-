#!/bin/bash
#
# Integration tests for the write path — phase 2, step 4.
#
#   ./tests/it/write_path.sh [port]
#
# Step 3's tests assert on the server's log because the server had no way to
# answer. These assert on what comes BACK over the socket, which is only
# possible now that sendToClient and POLLOUT exist. Until the dispatcher lands
# in step 5, handleLine echoes each line, so the echo stands in for a reply.

PORT=${1:-6692}
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

"$ROOT/ircserv" "$PORT" secret > "$LOG" 2>&1 &
SRV=$!
sleep 0.5
if ! kill -0 "$SRV" 2>/dev/null; then
	echo "servidor nao subiu na porta $PORT:"
	cat "$LOG"
	exit 1
fi
echo "write path (porta $PORT, pid $SRV)"

# 1. The echo comes back, CRLF-terminated. `read` strips the LF; the CR it
#    leaves behind is the proof that the line ending is CRLF and not bare LF.
REPLY=$(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'hello world\r\n' >&3
	IFS= read -r -t 2 line <&3
	printf '%s' "$line" | od -c | head -2 | tr -s ' '
)
check "a resposta termina em CRLF" \
	"0000000 h e l l o w o r l d \r" \
	"$(echo "$REPLY" | head -1)"

# 2. A line over the limit comes back truncated to 510, CRLF included = 512.
SIZE=$(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf '%0.sa' $(seq 1 600) >&3
	printf '\r\n' >&3
	IFS= read -r -t 2 line <&3
	printf '%s' "$line" | tr -d '\r' | wc -c
)
check "linha de 600 volta truncada em 510" "510" "$SIZE"

# 3. The reason recorded by step 3 now actually reaches the client. This is the
#    best-effort flush in reapDisconnected doing its job: the client is already
#    marked when the ERROR is queued, so without that flush it would be closed
#    with the message still sitting in the queue.
ERR=$(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf '%0.sx' $(seq 1 5000) >&3
	IFS= read -r -t 2 line <&3
	printf '%s' "$line" | tr -d '\r'
)
check "flood recebe ERROR antes de o socket fechar" \
	"ERROR :Request too long" "$ERR"

# 4. Two commands in one packet produce two separate replies, in order.
TWO=$(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'ONE\r\nTWO\r\n' >&3
	IFS= read -r -t 2 a <&3
	IFS= read -r -t 2 b <&3
	printf '%s,%s' "$(printf '%s' "$a" | tr -d '\r')" \
		"$(printf '%s' "$b" | tr -d '\r')"
)
check "dois comandos num pacote geram duas respostas, na ordem" "ONE,TWO" "$TWO"

# 5. The loop must not spin now that POLLOUT exists. Armed unconditionally it
#    is always ready, and this is where that shows up.
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'keepalive\r\n' >&3
	sleep 6
) &
IDLE=$!
sleep 1
A=$(awk '{print $14+$15}' /proc/"$SRV"/stat)
sleep 3
B=$(awk '{print $14+$15}' /proc/"$SRV"/stat)
check "CPU ociosa com cliente conectado e fila vazia" "0" "$((B - A))"
kill -9 "$IDLE" 2>/dev/null

kill -0 "$SRV" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor continua vivo no fim" "vivo" "$ALIVE"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
