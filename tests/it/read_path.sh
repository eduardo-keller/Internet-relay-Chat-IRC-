#!/bin/bash
#
# Integration tests for the READ path — packet reassembly.
#
#   ./tests/it/read_path.sh [port]
#
# Not part of `make test`: these need a real socket, so they live outside the
# graded build (decision D6 in docs/FASE2.md).
#
# Every assertion is made on what comes BACK over the socket. Until step 3
# these read the server's log instead, because the server had no way to answer;
# since step 5 an unknown command is answered with 421, which makes "the server
# saw exactly one command" observable from the client side — a much better
# proof than a debug line we control.
#
# The one case that cannot be scripted is the subject's literal test, because
# Ctrl+D is a terminal action rather than a byte. Case 1 sends the same three
# fragments as three packets, which is precisely what Ctrl+D produces. Run the
# manual version too, since it is what the evaluator types:
#
#   nc -C 127.0.0.1 6667
#   com^Dman^Dd<Enter>        -> a single ":ircserv 421 * COMMAND ..." comes back

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

"$ROOT/ircserv" "$PORT" secret > "$LOG" 2>&1 &
SRV=$!
sleep 0.5
if ! kill -0 "$SRV" 2>/dev/null; then
	echo "servidor nao subiu na porta $PORT:"
	cat "$LOG"
	exit 1
fi
echo "read path (porta $PORT, pid $SRV)"

# 1. The subject's test: one command split across three packets. One reply
#    means one command was rebuilt; three replies would mean three fragments.
OUT=$(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'com' >&3; sleep 0.3
	printf 'man' >&3; sleep 0.3
	printf 'd\r\n' >&3
	while IFS= read -r -t 1 l <&3; do printf '%s\n' "$l" | tr -d '\r'; done
)
check "pacote parcial em 3 pedacos vira UM comando" \
	":ircserv 421 * COMMAND :Unknown command" "$OUT"

# 2. Two complete commands in a single write drain as two.
OUT=$(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'ONE\r\nTWO\r\n' >&3
	while IFS= read -r -t 1 l <&3; do printf '%s\n' "$l" | tr -d '\r'; done
)
check "dois comandos num pacote so viram DOIS" \
	":ircserv 421 * ONE :Unknown command
:ircserv 421 * TWO :Unknown command" "$OUT"

# 3. A bare LF ends a line too. nc sends this without -C, and so does anything
#    piped through echo.
OUT=$(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'BARELF\n' >&3
	while IFS= read -r -t 1 l <&3; do printf '%s\n' "$l" | tr -d '\r'; done
)
check "LF sozinho, sem CR, funciona igual" \
	":ircserv 421 * BARELF :Unknown command" "$OUT"

# 4. A flood with no line ending is refused rather than buffered without bound.
OUT=$(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf '%0.sx' $(seq 1 5000) >&3
	IFS= read -r -t 2 l <&3
	printf '%s' "$l" | tr -d '\r'
)
check "5000 bytes sem CRLF -> ERROR e desconecta" "ERROR :Request too long" "$OUT"

# 5. Everything above ran against a server that has to still be standing.
kill -0 "$SRV" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor continua vivo no fim" "vivo" "$ALIVE"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
