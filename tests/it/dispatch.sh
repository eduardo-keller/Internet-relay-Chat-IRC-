#!/bin/bash
#
# Integration tests for the DISPATCHER — phase 2, step 5.
#
#   ./tests/it/dispatch.sh [port]
#
# Every transport handler is still an empty body at this point, and that is
# what makes these tests sharp: a handler that runs produces no output, so any
# reply seen here came from the dispatcher itself and nowhere else.

PORT=${1:-6696}
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

# Sends whatever it is given and collects every line that comes back.
talk()
{
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf "$1" >&3
	while IFS= read -r -t 1 l <&3; do printf '%s\n' "$l" | tr -d '\r'; done
}

"$ROOT/ircserv" "$PORT" secret > "$LOG" 2>&1 &
SRV=$!
sleep 0.5
if ! kill -0 "$SRV" 2>/dev/null; then
	echo "servidor nao subiu na porta $PORT:"
	cat "$LOG"
	exit 1
fi
echo "dispatch (porta $PORT, pid $SRV)"

# 1. An unknown command is 421, and it is answered even to an unregistered
#    client: FOO does not exist for anybody, so 451 would wrongly imply that
#    registering first would make it work.
check "comando desconhecido -> 421" \
	":ircserv 421 * FOO :Unknown command" "$(talk 'FOO\r\n')"

# 2. IRC command names are case-insensitive. The reply echoes the uppercased
#    form, which is also the proof that the lookup uppercased before searching.
check "mesmo comando em minusculas -> mesmo 421" \
	":ircserv 421 * FOO :Unknown command" "$(talk 'foo\r\n')"

# 3. A command that EXISTS but needs registration is 451. PONG is in the table
#    and deliberately not in the pre-registration set, which makes it the one
#    stable way to exercise the gate before PASS/NICK/USER have bodies.
check "comando conhecido antes do registro -> 451" \
	":ircserv 451 * :You have not registered" "$(talk 'PONG token\r\n')"

# 4. The six commands allowed before registration pass the gate. Their bodies
#    are empty in this step, so silence is exactly right: no 451 means the gate
#    let them through, and no other reply means nothing else fired.
check "PASS passa pela porta do registro (corpo vazio, sem resposta)" \
	"" "$(talk 'PASS secret\r\n')"
#    CAP passes the same gate, but it is no longer silent (D17 supersedes D2):
#    what this file cares about is that it does NOT take the 421 path and does
#    NOT take the 451 path. The content of the answer is asserted in session.sh.
check "CAP passa pela porta do registro e nao vira 421 nem 451" \
	":ircserv CAP * LS :" "$(talk 'CAP LS 302\r\n')"

# 5. Blank lines produce NO reply at all. nc sends one every time the user
#    presses Enter on an empty prompt; answering them would flood the client.
check "linhas vazias nao geram resposta" \
	":ircserv 421 * FOO :Unknown command" "$(talk '\r\n\r\nFOO\r\n')"

# 6. THE DAY ARRIVED. JOIN was answered 421 :Unknown command until phase 3
#    step 1 registered it, and this assertion said so in its own name: it was
#    written to fail as the reminder to update it. It now checks the other side
#    of the same rule — a KNOWN command from an unregistered client is 451.
check "JOIN e conhecido, e antes do registro da 451" \
	":ircserv 451 * :You have not registered" "$(talk 'JOIN #x\r\n')"

#    And the distinction it was really guarding still holds: a command that
#    exists for nobody is 421, decided BEFORE the registration gate, because
#    "you have not registered" would imply that registering makes NOPE work.
check "um comando que nao existe continua sendo 421, nao 451" \
	":ircserv 421 * NOPE :Unknown command" "$(talk 'NOPE #x\r\n')"

kill -0 "$SRV" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor continua vivo no fim" "vivo" "$ALIVE"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
