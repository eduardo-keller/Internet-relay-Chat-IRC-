#!/bin/bash
#
# Integration tests for KICK — phase 3, step 7.
#
#   ./tests/it/kick.sh [port]
#
# Unlike the JOIN gates and +t, KICK is fully reachable over a socket already:
# the client that creates a channel is its operator, so an operator and a
# victim can both be arranged without MODE existing. This is the first operator
# command proved end to end.
#
# TWO PERSISTENT CLIENTS, EACH DRIVEN THROUGH A FIFO. The scripts before this
# one only ever needed a connection that talked once and hung up, so `talk` was
# enough; KICK needs a conversation — the operator has to still be there, still
# an operator, when the victim has already joined. A named pipe held open by
# the main shell is what makes a long-lived client scriptable line by line.

PORT=${1:-6715}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LOG=$(mktemp)
OUTOP=$(mktemp)
OUTVIC=$(mktemp)
FIFOOP=$(mktemp -u)
FIFOVIC=$(mktemp -u)
PASSED=0
FAILED=0

cleanup()
{
	exec 4>&- 2>/dev/null
	exec 5>&- 2>/dev/null
	[ -n "$OP" ] && kill -9 $(pgrep -P "$OP") "$OP" 2>/dev/null
	[ -n "$VIC" ] && kill -9 $(pgrep -P "$VIC") "$VIC" 2>/dev/null
	if [ -n "$SRV" ]; then
		kill -INT "$SRV" 2>/dev/null
		wait "$SRV" 2>/dev/null
	fi
	rm -f "$LOG" "$OUTOP" "$OUTVIC" "$FIFOOP" "$FIFOVIC"
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

# CR stripped on READ — see tests/it/privmsg.sh for why not on write.
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
echo "kick (porta $PORT, pid $SRV)"

REG='PASS secret\r\nNICK zeca\r\nUSER zeca 0 * :Zeca\r\n'

# 1. The errors that need no channel.
check "KICK so com o canal -> 461" \
	":ircserv 461 zeca KICK :Not enough parameters" \
	"$(talk "$REG"'KICK #x\r\n' | tail -1)"

check "KICK em canal inexistente -> 403" \
	":ircserv 403 zeca #nope :No such channel" \
	"$(talk "$REG"'KICK #nope alguem\r\n' | tail -1)"

check "KICK antes do registro -> 451" \
	":ircserv 451 * :You have not registered" \
	"$(talk 'KICK #x y\r\n')"

# 2. THE OPERATOR. It joins first, so it creates #sala and owns it.
mkfifo "$FIFOOP" "$FIFOVIC"
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	cat "$FIFOOP" >&3 &
	cat <&3 > "$OUTOP"
) &
OP=$!
disown
# Held open by THIS shell, so the pipe does not see EOF between writes.
exec 4>"$FIFOOP"
printf 'PASS secret\r\nNICK chefe\r\nUSER chefe 0 * :Chefe\r\nJOIN #sala\r\n' >&4
sleep 2

(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	cat "$FIFOVIC" >&3 &
	cat <&3 > "$OUTVIC"
) &
VIC=$!
disown
exec 5>"$FIFOVIC"
printf 'PASS secret\r\nNICK vitima\r\nUSER vitima 0 * :Vitima\r\nJOIN #sala\r\n' >&5
sleep 2

check "o criador do canal e operador" \
	"1" "$(seen "$OUTVIC" | grep -c ' 353 vitima = #sala :.*@chefe')"

# 3. A non-member is 442 — being outside is the first thing wrong, before the
#    missing badge.
check "estranho tentando expulsar -> 442" \
	"1" "$(talk "$REG"'KICK #sala vitima\r\n' | grep -c " 442 zeca #sala :You're not on that channel")"

# 4. A plain member is 482. This is the line that makes the operator mean
#    something.
printf 'KICK #sala chefe :quero mandar eu\r\n' >&5
sleep 1
check "membro comum tentando expulsar -> 482" \
	"1" "$(seen "$OUTVIC" | grep -c "^:ircserv 482 vitima #sala :You're not channel operator$")"
check "e o operador continua no canal" \
	"0" "$(seen "$OUTOP" | grep -c ' KICK #sala chefe')"

# 5. Somebody who is not in the channel is 441, not 401 (decision D10).
printf 'KICK #sala ninguem\r\n' >&4
sleep 1
check "expulsar quem nao esta no canal -> 441" \
	"1" "$(seen "$OUTOP" | grep -c "^:ircserv 441 chefe ninguem #sala :They aren't on that channel$")"

# 6. THE KICK ITSELF. The victim's own copy has to arrive — it is what closes
#    the channel window in their client.
printf 'KICK #sala vitima :comporte-se\r\n' >&4
sleep 1
check "a vitima recebe o proprio KICK" \
	"1" "$(seen "$OUTVIC" | grep -c '^:chefe!chefe@127.0.0.1 KICK #sala vitima :comporte-se$')"
check "e o operador ve o mesmo" \
	"1" "$(seen "$OUTOP" | grep -c '^:chefe!chefe@127.0.0.1 KICK #sala vitima :comporte-se$')"

#    And the victim really is out: talking into the channel now earns a 404,
#    which is the membership check answering.
printf 'PRIVMSG #sala :ainda estou ai?\r\n' >&5
sleep 1
check "a vitima saiu de verdade: falar no canal da 404" \
	"1" "$(seen "$OUTVIC" | grep -c '^:ircserv 404 vitima #sala :Cannot send to channel$')"

#    KICK ends a membership, not a connection: the victim is still connected
#    and can walk straight back in.
printf 'JOIN #sala\r\n' >&5
sleep 1
#    TWO 366 lines, not one: the victim's first JOIN, back at the top, produced
#    the first. The second is the one this case is about.
check "a vitima continua conectada e pode voltar" \
	"2" "$(seen "$OUTVIC" | grep -c '^:ircserv 366 vitima #sala :End of /NAMES list$')"

# 7. No reason means the kicker's nickname, and the nick is matched ignoring
#    case.
printf 'KICK #sala VITIMA\r\n' >&4
sleep 1
check "sem motivo, o motivo e o nick de quem expulsou" \
	"1" "$(seen "$OUTVIC" | grep -c '^:chefe!chefe@127.0.0.1 KICK #sala vitima :chefe$')"

kill -0 "$SRV" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor continua vivo no fim" "vivo" "$ALIVE"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
