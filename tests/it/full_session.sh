#!/bin/bash
#
# Full-session soak under valgrind — phase 3, step 13.
#
#   ./tests/it/full_session.sh [port]
#
# Every other script proves one command. This one exercises the whole surface
# in a single server lifetime, under valgrind, and asks a different question:
# does anything leak, dangle or double-free once channels, invitations,
# operator sets and disconnects are all in play at once?
#
# It closes two items the phase 4 list had been carrying:
#
#   * "valgrind sem vazamento — refazer com os comandos de canal". The earlier
#     run had 27 clients connected at SIGINT but no channels, so nothing ever
#     populated a member, operator or invite set.
#   * "QUIT colado com outra linha no mesmo pacote — repetir com canais quando
#     o JOIN existir". A QUIT sharing a packet with a PRIVMSG, sent by a client
#     that is in two channels and holds an invitation to a third, is the case
#     the deferred disconnect exists for.

PORT=${1:-6719}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LOG=$(mktemp)
OUTA=$(mktemp)
OUTB=$(mktemp)
FIFOA=$(mktemp -u)
FIFOB=$(mktemp -u)
PASSED=0
FAILED=0

cleanup()
{
	exec 4>&- 2>/dev/null
	exec 5>&- 2>/dev/null
	[ -n "$A" ] && kill -9 $(pgrep -P "$A") "$A" 2>/dev/null
	[ -n "$B" ] && kill -9 $(pgrep -P "$B") "$B" 2>/dev/null
	[ -n "$VG" ] && kill -9 "$VG" 2>/dev/null
	rm -f "$LOG" "$OUTA" "$OUTB" "$FIFOA" "$FIFOB"
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

seen()
{
	tr -d '\r' < "$1"
}

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
echo "full session (porta $PORT, pid $VG, sob valgrind)"

# Two long-lived clients, driven line by line.
mkfifo "$FIFOA" "$FIFOB"
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	cat "$FIFOA" >&3 &
	cat <&3 > "$OUTA"
) &
A=$!
disown
exec 4>"$FIFOA"
printf 'PASS secret\r\nNICK ana\r\nUSER ana 0 * :Ana\r\n' >&4
sleep 2

(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	cat "$FIFOB" >&3 &
	cat <&3 > "$OUTB"
) &
B=$!
disown
exec 5>"$FIFOB"
printf 'PASS secret\r\nNICK beto\r\nUSER beto 0 * :Beto\r\n' >&5
sleep 2

# 1. Channels, topics, modes, operators, invitations — all of it live at once.
printf 'JOIN #um,#dois\r\n' >&4
sleep 1
printf 'JOIN #um\r\n' >&5
sleep 1
printf 'TOPIC #um :conversa geral\r\nMODE #um +tk chave\r\nMODE #um +o beto\r\n' >&4
sleep 1
printf 'MODE #dois +il 5\r\n' >&4
sleep 1
printf 'INVITE beto #dois\r\n' >&4
sleep 1
printf 'PRIVMSG #um :ola\r\nPRIVMSG beto :privado\r\n' >&4
sleep 1

check "o canal com chave e tranca aparece no 324" \
	":ircserv 324 beto #um +tk chave" \
	"$(printf 'MODE #um\r\n' >&5; sleep 1; seen "$OUTB" | grep ' 324 ' | tail -1)"
check "a mensagem de canal chegou" \
	":ana!ana@127.0.0.1 PRIVMSG #um :ola" \
	"$(seen "$OUTB" | grep ' PRIVMSG #um ' | tail -1)"
check "a mensagem privada chegou" \
	":ana!ana@127.0.0.1 PRIVMSG beto :privado" \
	"$(seen "$OUTB" | grep ' PRIVMSG beto ' | tail -1)"

# 2. A NICK CHANGE while in channels: it must reach the peers exactly once,
#    however many channels are shared.
printf 'NICK aninha\r\n' >&4
sleep 1
check "a troca de nick chega aos pares uma unica vez" \
	"1" "$(seen "$OUTB" | grep -c '^:ana!ana@127.0.0.1 NICK :aninha$')"

# 3. THE USE-AFTER-FREE CASE, now with channels. This client joins two
#    channels, is invited to a third, and then sends QUIT glued to a PRIVMSG in
#    one write. The QUIT must be honoured, the PRIVMSG must never be
#    dispatched, and the sweep must clear the client out of every member,
#    operator and invite set before the delete.
talk 'PASS secret\r\nNICK efemera\r\nUSER efemera 0 * :Efemera\r\nJOIN #tres,#quatro\r\nQUIT :ja vou\r\nPRIVMSG #tres :nao devia sair\r\n' > /dev/null
sleep 2
check "o QUIT colado com outra linha foi atendido" \
	"1" "$(grep -c 'closing fd .*(ja vou)' "$LOG")"

# 4. A client killed outright, with no QUIT, while it holds a membership, an
#    operator badge and an invitation.
printf 'JOIN #cinco\r\nMODE #cinco +i\r\nINVITE aninha #cinco\r\n' >&5
sleep 1
kill -9 $(pgrep -P "$B") "$B" 2>/dev/null
B=
sleep 2

#    And somebody walks the member sets afterwards, which is where a stale
#    pointer would be dereferenced.
check "o servidor sobrevive a quem morreu segurando canal, badge e convite" \
	"1" "$(talk 'PASS secret\r\nNICK carla\r\nUSER carla 0 * :Carla\r\nJOIN #um chave\r\n' | grep -c ' 366 carla #um ')"

# 5. Shut down with clients still connected and channels still populated. This
#    is the path where ~Server has to free every Channel and every Client.
kill -0 "$VG" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor vivo antes do encerramento" "vivo" "$ALIVE"

exec 4>&- 2>/dev/null
kill -INT "$VG"
wait "$VG"
VGEXIT=$?
VG=

check "valgrind sem erros (42 = erro detectado)" "0" "$VGEXIT"
check "nada definitivamente vazado" \
	"0" "$(grep -c 'definitely lost: [^0]' "$LOG")"
check "nada indiretamente vazado" \
	"0" "$(grep -c 'indirectly lost: [^0]' "$LOG")"
check "nenhuma leitura ou escrita em memoria liberada" \
	"0" "$(grep -c 'Invalid read\|Invalid write' "$LOG")"
check "nenhum free invalido" \
	"0" "$(grep -c 'Invalid free' "$LOG")"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
