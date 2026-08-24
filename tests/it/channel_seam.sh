#!/bin/bash
#
# Integration test for the channel seam — phase 2, step 4.5.
#
#   ./tests/it/channel_seam.sh [port]
#
# THIS TEST EXISTS FOR ONE BUG: a Client deleted while a Channel still holds a
# pointer to it. Channel's member, operator and invite sets are non-owning, so
# reapDisconnected must run sweepChannels before the delete. Get that wrong and
# nothing looks broken — until the next person to speak in the channel walks
# the member set and dereferences freed memory.
#
# That is why this one runs under valgrind: the failure is silent without it.
# An "invalid read of size 8" in the output is the bug; the script fails if
# valgrind reports any error at all.
#
# IT NEEDS A WORKING JOIN, and therefore the domain track's cmdJoin plus its
# entry in src/CommandTable.cpp. Step 4.5 ran it against a temporary scaffold
# in handleLine; step 5 replaced that with the real dispatcher, so until those
# entries are uncommented the script SKIPS instead of failing — it probes for
# JOIN below and reactivates itself the day the handler lands.
#
# When it does: registration comes first, so the JOIN lines here become a full
# PASS/NICK/USER burst followed by JOIN #room. That makes this a better test
# than the scaffolded version ever was.

PORT=${1:-6694}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LOG=$(mktemp)
PASSED=0
FAILED=0

cleanup()
{
	[ -n "$VG" ] && kill -9 "$VG" 2>/dev/null
	[ -n "$A" ] && kill -9 "$A" 2>/dev/null
	[ -n "$B" ] && kill -9 "$B" 2>/dev/null
	rm -f "$LOG" "$OUTA" "$OUTB" "$OUTC"
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
echo "channel seam (porta $PORT, pid $VG, sob valgrind)"

# THE CLIENTS REGISTER FIRST. JOIN is behind the dispatcher's registration
# gate, so a bare "JOIN #room" earns a 451 and never reaches a channel. Until
# phase 3 step 1 this script probed for a 421 and skipped itself, because
# cmdJoin had no body at all; that probe is gone, and so is the skip.
#
# Each client holds its socket open in a subshell and writes everything it
# receives to a file, so that the connection can be killed abruptly — no QUIT,
# no clean close — while the bytes it saw remain readable afterwards.
OUTA=$(mktemp)
OUTB=$(mktemp)
OUTC=$(mktemp)

(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'PASS secret\r\nNICK alice\r\nUSER alice 0 * :Alice\r\nJOIN #room\r\n' >&3
	cat <&3 > "$OUTA" &
	sleep 60
) &
A=$!
disown
sleep 2

(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'PASS secret\r\nNICK bob\r\nUSER bob 0 * :Bob\r\nJOIN #room\r\n' >&3
	cat <&3 > "$OUTB" &
	sleep 60
) &
B=$!
disown
sleep 2

# Both are in: bob got his own JOIN echo, and alice — who was already there —
# was told about him. The second half is the broadcast, and it is the one that
# proves they are in the SAME channel object.
check "bob entrou no canal" \
	"1" "$(grep -c 'JOIN #room' "$OUTB")"
check "alice foi avisada da entrada do bob" \
	"1" "$(grep -c '^:bob!bob@127.0.0.1 JOIN #room' "$OUTA")"
check "bob viu os dois nicks na lista de NAMES" \
	"1" "$(grep -c ' 353 bob = #room :.*alice.*' "$OUTB")"

# KILL ONE OUTRIGHT: no QUIT, no clean close, exactly like a client that was
# kill -9'd or whose laptop lost the network. The server reaps it, and
# sweepChannels must take it out of #room BEFORE the delete.
# THE CHILDREN GO TOO. The subshell spawned a `cat` that holds the socket open,
# and killing only the parent leaves it running — the connection stays up, the
# server never reaps anything, and this test silently proves nothing. It was
# caught by the NAMES assertion below, which is the point of having it.
kill -9 $(pgrep -P "$A") "$A" 2>/dev/null
A=
sleep 2

# Now somebody walks #room's member set. This is the precise place a stale
# Client* would be dereferenced: cmdJoin broadcasts the arrival to every member
# and then builds the NAMES list out of the same set.
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'PASS secret\r\nNICK carol\r\nUSER carol 0 * :Carol\r\nJOIN #room\r\n' >&3
	cat <&3 > "$OUTC" &
	sleep 3
)
sleep 1

check "carol entrou no canal depois da morte da alice" \
	"1" "$(grep -c ' 366 carol #room ' "$OUTC")"

# And the dead client is GONE from the list, not merely unreferenced. A sweep
# that forgot the member set would still be listing alice here.
check "alice sumiu da lista de NAMES" \
	"0" "$(grep -c ' 353 carol .*alice' "$OUTC")"

kill -0 "$VG" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor sobreviveu ao morto no canal" "vivo" "$ALIVE"

# Everyone leaves, the channel empties out, and SIGINT unwinds the rest.
kill -9 $(pgrep -P "$B") "$B" 2>/dev/null
B=
sleep 1
kill -INT "$VG"
wait "$VG"
VGEXIT=$?
VG=

check "valgrind sem erros (42 = erro detectado)" "0" "$VGEXIT"

LEAKS=$(grep -c 'definitely lost: [^0]' "$LOG")
check "nada definitivamente vazado" "0" "$LEAKS"

INVALID=$(grep -c 'Invalid read\|Invalid write' "$LOG")
check "nenhuma leitura ou escrita em memoria liberada" "0" "$INVALID"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
