#!/bin/bash
#
# Integration tests for PART — phase 3, step 3.
#
#   ./tests/it/part.sh [port]
#
# The half a unit test cannot show is the LIFECYCLE across real connections:
# a channel that empties out is destroyed along with its topic, its modes and
# its operator list, so the next person through the door opens a brand new
# channel and owns it. Proving that needs the channel to have genuinely gone,
# which means watching a second connection discover it.

PORT=${1:-6709}
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

# THE BODY RUNS IN A SUBSHELL, unlike the copy of this helper in the other
# scripts, and the difference is not cosmetic.
#
# `exec 3<>` opens the socket in whatever shell runs it. Inside $( ) that is a
# subshell, so the connection dies with it — which is why the other scripts,
# where every call is a substitution, get away without this. This file calls
# talk twice for its side effect alone ("talk ... > /dev/null"), and those run
# in the MAIN shell: the socket stayed open for the rest of the script, still
# registered, still holding its nickname. Every later client asking for the
# same nick got 433, never registered, and its JOIN and PART came back as 451.
#
# The failure looked like a broken PART. It was a leaked file descriptor in the
# test.
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
echo "part (porta $PORT, pid $SRV)"

REG='PASS secret\r\nNICK alice\r\nUSER alice 0 * :Alice\r\n'

# 1. The errors, and the distinction between them: 403 says the room is not
#    there, 442 says it is and you are not in it.
check "PART sem parametro -> 461" \
	":ircserv 461 alice PART :Not enough parameters" \
	"$(talk "$REG"'PART\r\n' | tail -1)"

check "PART de canal inexistente -> 403" \
	":ircserv 403 alice #nope :No such channel" \
	"$(talk "$REG"'PART #nope\r\n' | tail -1)"

check "PART antes do registro -> 451" \
	":ircserv 451 * :You have not registered" \
	"$(talk 'PART #x\r\n')"

# 2. THE LEAVER GETS ITS OWN ECHO. That is what closes the window in irssi;
#    without it the client is left with a channel it thinks it is still in.
OUT=$(talk "$REG"'JOIN #room\r\nPART #room :ate mais\r\n')
check "quem sai recebe o proprio PART, com o motivo" \
	"1" "$(printf '%s\n' "$OUT" | grep -c '^:alice!alice@127.0.0.1 PART #room :ate mais$')"

#    And with no reason there is no trailing parameter at all — no invented one.
OUT=$(talk "$REG"'JOIN #room2\r\nPART #room2\r\n')
check "PART sem motivo nao inventa um" \
	"1" "$(printf '%s\n' "$OUT" | grep -c '^:alice!alice@127.0.0.1 PART #room2$')"

# 3. A LIVE WITNESS. One connection stays in the channel while another joins
#    and leaves; the witness has to see both events.
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'PASS secret\r\nNICK ana\r\nUSER ana 0 * :Ana\r\nJOIN #sala\r\n' >&3
	cat <&3 > "$OUTA" &
	sleep 30
) &
A=$!
disown
sleep 1

talk 'PASS secret\r\nNICK beto\r\nUSER beto 0 * :Beto\r\nJOIN #sala\r\nPART #sala :tchau\r\n' > /dev/null
sleep 1

check "quem ficou viu a entrada" \
	"1" "$(grep -c '^:beto!beto@127.0.0.1 JOIN #sala' "$OUTA")"
check "e viu a saida, com o motivo" \
	"1" "$(grep -c '^:beto!beto@127.0.0.1 PART #sala :tchau' "$OUTA")"

#    The witness is still in, so the channel is still there: a fresh client
#    joining it does NOT become its operator.
OUT=$(talk "$REG"'JOIN #sala\r\n')
check "o canal continua vivo enquanto alguem esta nele" \
	"1" "$(printf '%s\n' "$OUT" | grep -c ' 353 alice = #sala :.*@ana')"
check "e quem chega depois nao vira operador" \
	"0" "$(printf '%s\n' "$OUT" | grep -c ' 353 alice = #sala :.*@alice')"

# 4. THE LIFECYCLE. A channel is opened, given a topic-free identity by its
#    creator, emptied, and then re-opened by somebody else. The second creator
#    owns it — which is only true if the first channel was really destroyed.
talk "$REG"'JOIN #efemero\r\nPART #efemero\r\n' > /dev/null
OUT=$(talk 'PASS secret\r\nNICK carol\r\nUSER carol 0 * :Carol\r\nJOIN #efemero\r\n')
check "canal vazio e destruido, e quem o recria vira operador" \
	"1" "$(printf '%s\n' "$OUT" | grep -c ' 353 carol = #efemero :@carol')"

# 5. A list, like JOIN, and one bad name does not cancel the rest.
OUT=$(talk "$REG"'JOIN #l1,#l2\r\nPART #l1,#l2\r\n')
check "PART aceita lista, com um eco por canal" \
	"2" "$(printf '%s\n' "$OUT" | grep -c '^:alice!alice@127.0.0.1 PART ')"

OUT=$(talk "$REG"'JOIN #ok1\r\nPART #naoexiste,#ok1\r\n')
check "nome ruim na lista nao impede a saida dos outros" \
	"1" "$(printf '%s\n' "$OUT" | grep -c '^:alice!alice@127.0.0.1 PART #ok1$')"
check "e o nome ruim leva 403" \
	"1" "$(printf '%s\n' "$OUT" | grep -c ' 403 alice #naoexiste ')"

kill -0 "$SRV" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor continua vivo no fim" "vivo" "$ALIVE"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
