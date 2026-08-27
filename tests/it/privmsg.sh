#!/bin/bash
#
# Integration tests for PRIVMSG — phase 3, step 4.
#
#   ./tests/it/privmsg.sh [port]
#
# Two things live here that a unit test cannot reach:
#
#   * DELIVERY TO A NICKNAME. findClientByNick reads Server::_clients, and a
#     client only enters that map through accept() — so in a unit test it
#     always returns NULL and only the 401 is provable. The happy path needs
#     two real connections.
#   * THE 504-BYTE CASE from the phase 4 list. A PRIVMSG that is perfectly
#     legal on the way in can exceed 512 on the way out once
#     ":nick!user@host " is prepended, and it must arrive truncated at 510
#     rather than whole. The mechanism is tested in tests/test_server.cpp; this
#     is the first time a real PRIVMSG exercises it.

PORT=${1:-6711}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LOG=$(mktemp)
OUTA=$(mktemp)
OUTB=$(mktemp)
PASSED=0
FAILED=0

cleanup()
{
	[ -n "$A" ] && kill -9 $(pgrep -P "$A") "$A" 2>/dev/null
	[ -n "$B" ] && kill -9 $(pgrep -P "$B") "$B" 2>/dev/null
	if [ -n "$SRV" ]; then
		kill -INT "$SRV" 2>/dev/null
		wait "$SRV" 2>/dev/null
	fi
	rm -f "$LOG" "$OUTA" "$OUTB"
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

# The body runs in a subshell so that the socket dies with the call even when
# the output is thrown away — see the long note in tests/it/part.sh, where a
# leaked descriptor made three cases fail against a correct server.
# What a witness actually received, with the CR stripped.
#
# THE STRIPPING HAPPENS ON READ, NOT ON WRITE, and that is not arbitrary: the
# obvious "tr -d '\r' <&3 > file &" in the capture subshell block-buffers its
# output through stdio, so nothing reaches the file until the process ends and
# every assertion reads an empty file. cat writes through. Keeping the raw
# bytes on disk and filtering here also means the file still holds exactly what
# came off the socket, which is what you want when a case fails.
#
# Without it, a grep anchored with $ never matches — every IRC line ends in CR
# — and a length check reads one byte too many, which is how the 510 assertion
# below first came back as 511.
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
echo "privmsg (porta $PORT, pid $SRV)"

REG='PASS secret\r\nNICK zeca\r\nUSER zeca 0 * :Zeca\r\n'

# 1. The error paths.
check "PRIVMSG sem alvo -> 411, nomeando o comando" \
	":ircserv 411 zeca :No recipient given (PRIVMSG)" \
	"$(talk "$REG"'PRIVMSG\r\n' | tail -1)"

check "PRIVMSG sem texto -> 412" \
	":ircserv 412 zeca :No text to send" \
	"$(talk "$REG"'PRIVMSG #x\r\n' | tail -1)"

check "PRIVMSG para canal inexistente -> 403" \
	":ircserv 403 zeca #nope :No such channel" \
	"$(talk "$REG"'PRIVMSG #nope :ola\r\n' | tail -1)"

check "PRIVMSG para nick inexistente -> 401" \
	":ircserv 401 zeca ninguem :No such nick/channel" \
	"$(talk "$REG"'PRIVMSG ninguem :ola\r\n' | tail -1)"

check "PRIVMSG antes do registro -> 451" \
	":ircserv 451 * :You have not registered" \
	"$(talk 'PRIVMSG #x :oi\r\n')"

# 2. TWO REAL CLIENTS, both in one channel, each holding its socket open.
(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'PASS secret\r\nNICK ana\r\nUSER ana 0 * :Ana\r\nJOIN #sala\r\n' >&3
	cat <&3 > "$OUTA" &
	sleep 40
) &
A=$!
disown
sleep 1

(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf 'PASS secret\r\nNICK beto\r\nUSER beto 0 * :Beto\r\nJOIN #sala\r\n' >&3
	cat <&3 > "$OUTB" &
	sleep 40
) &
B=$!
disown
sleep 2

#    The subject's own sentence, in one assertion: a message sent to a channel
#    reaches every other client that joined it.
talk 'PASS secret\r\nNICK caio\r\nUSER caio 0 * :Caio\r\nJOIN #sala\r\nPRIVMSG #sala :ola a todos\r\n' > /dev/null
sleep 1
check "a mensagem no canal chega ao primeiro membro" \
	"1" "$(seen "$OUTA" | grep -c '^:caio!caio@127.0.0.1 PRIVMSG #sala :ola a todos$')"
check "e ao segundo" \
	"1" "$(seen "$OUTB" | grep -c '^:caio!caio@127.0.0.1 PRIVMSG #sala :ola a todos$')"

#    A non-member cannot talk into the room (D13), and nothing leaks.
BEFORE=$(seen "$OUTA" | grep -c 'PRIVMSG #sala')
check "estranho falando no canal -> 404" \
	"1" "$(talk "$REG"'PRIVMSG #sala :deixem eu falar\r\n' | grep -c ' 404 zeca #sala :Cannot send to channel')"
sleep 1
check "e a mensagem dele nao chega a ninguem" \
	"$BEFORE" "$(seen "$OUTA" | grep -c 'PRIVMSG #sala')"

# 3. PRIVATE MESSAGE TO A NICKNAME — the half unit tests cannot prove.
talk 'PASS secret\r\nNICK dina\r\nUSER dina 0 * :Dina\r\nPRIVMSG ana :so pra voce\r\n' > /dev/null
sleep 1
check "mensagem privada chega ao destinatario" \
	"1" "$(seen "$OUTA" | grep -c '^:dina!dina@127.0.0.1 PRIVMSG ana :so pra voce$')"
check "e nao chega a mais ninguem" \
	"0" "$(seen "$OUTB" | grep -c 'so pra voce')"

#    The nickname is matched ignoring case, and the line is addressed with the
#    recipient's OWN spelling — which is what their client matches its query
#    window against.
talk 'PASS secret\r\nNICK elba\r\nUSER elba 0 * :Elba\r\nPRIVMSG ANA :maiusculas\r\n' > /dev/null
sleep 1
check "nick e casado ignorando maiusculas, e entregue com a grafia do dono" \
	"1" "$(seen "$OUTA" | grep -c '^:elba!elba@127.0.0.1 PRIVMSG ana :maiusculas$')"

# 3.5 A LIST OF TARGETS. RFC 2812 section 3.3.1 defines msgtarget as
#     "msgto *( "," msgto )", and this is the half the unit tests cannot reach:
#     delivery to two NICKNAMES at once needs two live sockets.
talk 'PASS secret\r\nNICK fabi\r\nUSER fabi 0 * :Fabi\r\nPRIVMSG ana,beto :para os dois\r\n' > /dev/null
sleep 1
check "lista de nicks entrega ao primeiro" \
	"1" "$(seen "$OUTA" | grep -c '^:fabi!fabi@127.0.0.1 PRIVMSG ana :para os dois$')"
check "e ao segundo, cada um endereçado a si" \
	"1" "$(seen "$OUTB" | grep -c '^:fabi!fabi@127.0.0.1 PRIVMSG beto :para os dois$')"

#     Nick and channel in the same list, and a bad target between them: the
#     refusal names ONLY the bad one, and the good targets are still served.
OUT=$(talk 'PASS secret\r\nNICK gabi\r\nUSER gabi 0 * :Gabi\r\nJOIN #sala\r\nPRIVMSG ana,#naoexiste,#sala :misto\r\n')
sleep 1
check "alvo invalido no meio da lista -> um 403 nomeando so ele" \
	"1" "$(printf '%s\n' "$OUT" | grep -c ' 403 gabi #naoexiste :No such channel')"
check "o nick antes dele foi servido" \
	"1" "$(seen "$OUTA" | grep -c '^:gabi!gabi@127.0.0.1 PRIVMSG ana :misto$')"
check "e o canal depois dele tambem" \
	"1" "$(seen "$OUTB" | grep -c '^:gabi!gabi@127.0.0.1 PRIVMSG #sala :misto$')"

#     An empty field names nobody, so it earns no error at all (D18).
OUT=$(talk 'PASS secret\r\nNICK hugo\r\nUSER hugo 0 * :Hugo\r\nPRIVMSG ana,,beto :campo vazio\r\n')
sleep 1
check "campo vazio na lista nao gera erro" \
	"0" "$(printf '%s\n' "$OUT" | grep -c ' 401 \| 403 ')"
check "e os alvos reais em volta recebem" \
	"1" "$(seen "$OUTB" | grep -c '^:hugo!hugo@127.0.0.1 PRIVMSG beto :campo vazio$')"

# 4. THE 504-BYTE CASE. The text below is legal on the way in — the whole
#    incoming line is under 512 — but the server prepends ":caio!caio@..." on
#    the way out and the result would be 528. It must arrive cut to exactly
#    510 bytes, CRLF excluded, and NOT whole.
LONG=$(printf 'x%.0s' $(seq 1 480))
talk "PASS secret\r\nNICK caio\r\nUSER caio 0 * :Caio\r\nJOIN #sala\r\nPRIVMSG #sala :$LONG\r\n" > /dev/null
sleep 1
RECEIVED=$(seen "$OUTA" | grep 'PRIVMSG #sala :x' | tail -1)
check "a linha longa chega truncada em exatamente 510 bytes" \
	"510" "${#RECEIVED}"

kill -0 "$SRV" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor continua vivo no fim" "vivo" "$ALIVE"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
