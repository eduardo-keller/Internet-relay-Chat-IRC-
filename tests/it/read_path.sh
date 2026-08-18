#!/bin/bash
#
# Integration tests for the read path — phase 2, step 3.
#
#   ./tests/it/read_path.sh [port]
#
# These are NOT part of `make test`: they need a real socket, so they live
# outside the graded build (decision D6 in docs/FASE2.md). They drive the
# server over TCP and assert on what it logs, because until step 4 the server
# has no way to write back to a client.
#
# The one case that cannot be scripted is the subject's literal test — nc -C
# with Ctrl+D between the fragments — because Ctrl+D is a terminal action, not
# a byte. Case 1 below sends the same three fragments in separate packets,
# which is exactly what Ctrl+D produces. Run the manual version too:
#
#   nc -C 127.0.0.1 6667
#   com^Dman^Dd<Enter>          -> the server must log ONE line: [command]

PORT=${1:-6690}
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

mark() { wc -l < "$LOG"; }
since() { tail -n +$(($1 + 1)) "$LOG"; }

"$ROOT/ircserv" "$PORT" secret > "$LOG" 2>&1 &
SRV=$!
sleep 0.5
if ! kill -0 "$SRV" 2>/dev/null; then
	echo "servidor nao subiu na porta $PORT:"
	cat "$LOG"
	exit 1
fi
echo "read path (porta $PORT, pid $SRV)"

# 1. The subject's test: one command split across three packets.
M=$(mark)
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'com' >&3; sleep 0.3
	printf 'man' >&3; sleep 0.3
	printf 'd\r\n' >&3; sleep 0.3
)
sleep 0.3
COUNT=$(since "$M" | grep -c 'line from')
TEXT=$(since "$M" | sed -n 's/.*line from [^ ]* \[\(.*\)\] (.*/\1/p')
check "pacote parcial em 3 pedacos vira UM comando" "1|command" "$COUNT|$TEXT"

# 2. Two complete commands in a single write must drain as two.
M=$(mark)
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'ONE\r\nTWO\r\n' >&3; sleep 0.3
)
sleep 0.3
COUNT=$(since "$M" | grep -c 'line from')
TEXT=$(since "$M" | sed -n 's/.*line from [^ ]* \[\(.*\)\] (.*/\1/p' | tr '\n' ',')
check "dois comandos num pacote so viram DOIS" "2|ONE,TWO," "$COUNT|$TEXT"

# 3. A line over the protocol limit is truncated to 510, not dropped and not
#    a disconnect. RFC 2812 section 2.3: 512 including CRLF.
M=$(mark)
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf '%0.sa' $(seq 1 600) >&3
	printf '\r\n' >&3; sleep 0.3
)
sleep 0.3
SIZE=$(since "$M" | sed -n 's/.*line from [^ ]* \[.*\] (\([0-9]*\) bytes)/\1/p')
check "600 bytes + CRLF viram uma linha de 510" "510" "$SIZE"

# 4. A flood with no line ending is refused instead of growing without bound.
M=$(mark)
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf '%0.sx' $(seq 1 5000) >&3; sleep 0.5
)
sleep 0.5
REASON=$(since "$M" | sed -n 's/.*closing fd [0-9]* (\(.*\))/\1/p')
check "5000 bytes sem CRLF -> desconecta" "Request too long" "$REASON"

# 5. A bare LF terminates a line too. nc sends this without -C, and so does
#    anything piped through echo.
M=$(mark)
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'BARELF\n' >&3; sleep 0.3
)
sleep 0.3
COUNT=$(since "$M" | grep -c 'line from')
TEXT=$(since "$M" | sed -n 's/.*line from [^ ]* \[\(.*\)\] (.*/\1/p')
check "LF sozinho, sem CR, funciona igual" "1|BARELF" "$COUNT|$TEXT"

# 6. The server survived all of it.
kill -0 "$SRV" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor continua vivo no fim" "vivo" "$ALIVE"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
