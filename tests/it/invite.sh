#!/bin/bash
#
# Integration tests for INVITE — phase 3, step 8.
#
#   ./tests/it/invite.sh [port]
#
# The half a unit test cannot reach is the invitation ARRIVING. findClientByNick
# reads Server::_clients, and a client only enters that map through accept(),
# so with objects on the stack the target is never found and only the 401 is
# provable.
#
# The +i case — an invited client walking into an invite-only channel while an
# uninvited one is refused — needs MODE to set the mode, and comes back here in
# step 10. What is reachable now is everything else: the numerics, the 341 that
# decision D8 fixes the order of, and the INVITE line itself.

PORT=${1:-6717}
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
	if [ -n "$SRV" ]; then
		kill -INT "$SRV" 2>/dev/null
		wait "$SRV" 2>/dev/null
	fi
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

"$ROOT/ircserv" "$PORT" secret > "$LOG" 2>&1 &
SRV=$!
sleep 0.5
if ! kill -0 "$SRV" 2>/dev/null; then
	echo "servidor nao subiu na porta $PORT:"
	cat "$LOG"
	exit 1
fi
echo "invite (porta $PORT, pid $SRV)"

REG='PASS secret\r\nNICK zeca\r\nUSER zeca 0 * :Zeca\r\n'

# 1. The errors.
check "INVITE so com o nick -> 461" \
	":ircserv 461 zeca INVITE :Not enough parameters" \
	"$(talk "$REG"'INVITE alguem\r\n' | tail -1)"

#    D21: a channel that does not exist is 442, not 403 — RFC 2812 section
#    3.2.7 does not list ERR_NOSUCHCHANNEL for INVITE at all.
check "INVITE para canal inexistente -> 442 (D21)" \
	":ircserv 442 zeca #nope :You're not on that channel" \
	"$(talk "$REG"'INVITE alguem #nope\r\n' | tail -1)"

check "INVITE antes do registro -> 451" \
	":ircserv 451 * :You have not registered" \
	"$(talk 'INVITE a #b\r\n')"

# 2. TWO PERSISTENT CLIENTS: a host inside a channel, and a guest outside it.
mkfifo "$FIFOA" "$FIFOB"
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	cat "$FIFOA" >&3 &
	cat <&3 > "$OUTA"
) &
A=$!
disown
exec 4>"$FIFOA"
printf 'PASS secret\r\nNICK anfitria\r\nUSER anfitria 0 * :Anfitria\r\nJOIN #festa\r\n' >&4
sleep 2

(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	cat "$FIFOB" >&3 &
	cat <&3 > "$OUTB"
) &
B=$!
disown
exec 5>"$FIFOB"
printf 'PASS secret\r\nNICK convidado\r\nUSER convidado 0 * :Convidado\r\n' >&5
sleep 2

#    Somebody outside the channel cannot invite into it.
check "quem nao esta no canal nao convida -> 442" \
	"1" "$(talk "$REG"'INVITE convidado #festa\r\n' | grep -c " 442 zeca #festa :You're not on that channel")"

#    A nickname nobody holds is 401.
printf 'INVITE fantasma #festa\r\n' >&4
sleep 1
check "convidar quem nao existe -> 401" \
	"1" "$(seen "$OUTA" | grep -c '^:ircserv 401 anfitria fantasma :No such nick/channel$')"

# 3. THE INVITATION ITSELF. The 341 goes back to whoever invited, in the
#    RFC 1459 order "<nick> <channel>" (decision D8), and the INVITE line goes
#    to the guest.
printf 'INVITE convidado #festa\r\n' >&4
sleep 1
check "341 volta a quem convidou, na ordem <nick> <canal> (D8)" \
	"1" "$(seen "$OUTA" | grep -c '^:ircserv 341 anfitria convidado #festa$')"
check "o convite chega ao convidado" \
	"1" "$(seen "$OUTB" | grep -c '^:anfitria!anfitria@127.0.0.1 INVITE convidado :#festa$')"

# 4. The guest walks in, and is then already on the channel.
printf 'JOIN #festa\r\n' >&5
sleep 1
check "o convidado entra no canal" \
	"1" "$(seen "$OUTB" | grep -c '^:ircserv 366 convidado #festa :End of /NAMES list$')"

printf 'INVITE convidado #festa\r\n' >&4
sleep 1
check "convidar alguem que ja esta no canal -> 443" \
	"1" "$(seen "$OUTA" | grep -c '^:ircserv 443 anfitria convidado #festa :is already on channel$')"

# 5. The nickname is matched ignoring case, and the invitation is addressed
#    with the guest's own spelling.
printf 'PART #festa\r\n' >&5
sleep 1
printf 'INVITE CONVIDADO #festa\r\n' >&4
sleep 1
#    TWO of them: the invitation in section 3 produced the first. Checking the
#    LAST matching line instead of counting them is the sturdier shape, and it
#    is what the remaining scripts use — this is the fourth time in the phase a
#    count was written as if the earlier identical line did not exist.
check "o nick do convidado e casado ignorando maiusculas" \
	":anfitria!anfitria@127.0.0.1 INVITE convidado :#festa" \
	"$(seen "$OUTB" | grep 'INVITE convidado' | tail -1)"

kill -0 "$SRV" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor continua vivo no fim" "vivo" "$ALIVE"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
