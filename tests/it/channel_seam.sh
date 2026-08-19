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

# Is JOIN wired up yet? 421 means the domain entries are still commented out in
# src/CommandTable.cpp, and without a way into a channel this test has nothing
# to sweep.
PROBE=$(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'JOIN #probe\r\n' >&3
	IFS= read -r -t 2 l <&3
	printf '%s' "$l" | tr -d '\r'
)
case "$PROBE" in
	*"421"*)
		echo "  SKIP este teste precisa do cmdJoin do DOMAIN na CommandTable"
		echo "       (resposta ao JOIN foi: $PROBE)"
		echo
		echo "0 passed, 0 failed — pulado"
		exit 0
		;;
esac

# Two clients in the same channel. Each holds its socket open in a subshell so
# that the connection can be killed abruptly, without a clean close.
( exec 3<>/dev/tcp/127.0.0.1/"$PORT"; printf 'JOIN #room\r\n' >&3; sleep 60 ) &
A=$!
( exec 3<>/dev/tcp/127.0.0.1/"$PORT"; printf 'JOIN #room\r\n' >&3; sleep 60 ) &
B=$!
sleep 1
JOINED=$(grep -c 'JOIN #room' "$LOG")
check "os dois clientes entraram no canal" "2" "$JOINED"

# Kill one of them outright: no QUIT, no clean close. The server reaps it, and
# sweepChannels must take it out of #room before the delete.
kill -9 "$A"
A=
sleep 1

# The survivor speaks. broadcastToPeers walks #room's member set — the exact
# place a stale pointer would be dereferenced.
( exec 3<>/dev/tcp/127.0.0.1/"$PORT"; printf 'JOIN #room\r\nstill here\r\n' >&3; sleep 1 )
sleep 1

kill -0 "$VG" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor sobreviveu ao morto no canal" "vivo" "$ALIVE"

# Everyone leaves, the channel empties out, and SIGINT unwinds the rest.
kill -9 "$B" 2>/dev/null
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
