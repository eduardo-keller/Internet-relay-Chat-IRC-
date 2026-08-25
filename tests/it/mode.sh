#!/bin/bash
#
# Integration tests for MODE — phase 3, step 0.6 onwards.
#
#   ./tests/it/mode.sh [port]
#
# WHAT THIS FILE IS FOR, TODAY: the unprompted "MODE <nick> +i" that irssi
# 1.4.5 sends about two seconds after registering, on every connection, without
# joining anything. It was captured on the wire in step 0 of docs/FASE3.md.
#
# With no MODE handler the dispatcher answers 421 :Unknown command, which puts
# an error line in irssi's status window — and the subject requires the
# reference client to connect "without encountering any error". Answering 403
# :No such channel instead would be the same mistake wearing better clothes,
# which is exactly how decision D2 went wrong with CAP. The right answer to a
# user mode, from a server that implements none, is nothing at all.
#
# Since steps 9 to 12 the rest of MODE lives here too, and with it the cases
# that every earlier script had to leave unfinished: the JOIN gates of step 2
# and the +t lock of step 6 could only be set through the Channel API in a unit
# test, because over a socket nothing but MODE can set a mode. They are end to
# end from here on.

PORT=${1:-6705}
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

seen()
{
	tr -d '\r' < "$1"
}

talk()
{
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	printf "$1" >&3
	while IFS= read -r -t 1 l <&3 2>/dev/null; do printf '%s\n' "$l" | tr -d '\r'; done
}

"$ROOT/ircserv" "$PORT" secret > "$LOG" 2>&1 &
SRV=$!
sleep 0.5
if ! kill -0 "$SRV" 2>/dev/null; then
	echo "servidor nao subiu na porta $PORT:"
	cat "$LOG"
	exit 1
fi
echo "mode (porta $PORT, pid $SRV)"

REG='PASS secret\r\nNICK alice\r\nUSER alice 0 * :Alice\r\n'

# 1. THE IRSSI CASE, byte for byte: register, then send exactly what irssi
#    sends by itself. Everything after the four registration numerics must be
#    empty — no 421, and no 403 either.
OUT=$(talk "$REG"'MODE alice +i\r\n')
check "MODE num nick nao responde nada (o caso real do irssi)" \
	"001 002 003 004 " \
	"$(printf '%s\n' "$OUT" | sed -n 's/^:ircserv \([0-9]*\) .*/\1/p' | tr '\n' ' ')"

# 2. And specifically not the two numerics it would be tempting to send.
check "MODE num nick nao vira 421" \
	"0" "$(printf '%s\n' "$OUT" | grep -c ' 421 ')"
check "MODE num nick nao vira 403" \
	"0" "$(printf '%s\n' "$OUT" | grep -c ' 403 ')"

# 3. A user-mode QUERY, with no flags, is just as silent.
check "MODE alice (consulta de modo de usuario) tambem e silencioso" \
	"001 002 003 004 " \
	"$(talk "$REG"'MODE alice\r\n' | sed -n 's/^:ircserv \([0-9]*\) .*/\1/p' | tr '\n' ' ')"

# 4. A channel that does not exist IS an error, and a different one from
#    "unknown command".
check "MODE num canal inexistente -> 403" \
	":ircserv 403 alice #nope :No such channel" \
	"$(talk "$REG"'MODE #nope\r\n' | tail -1)"

# 5. No parameter at all is 461.
check "MODE sem parametro -> 461" \
	":ircserv 461 alice MODE :Not enough parameters" \
	"$(talk "$REG"'MODE\r\n' | tail -1)"

# 6. MODE is behind the registration gate — it is not one of the six commands
#    allowed before PASS/NICK/USER.
check "MODE antes do registro -> 451" \
	":ircserv 451 * :You have not registered" \
	"$(talk 'MODE #chan\r\n')"

# ---------------------------------------------------------------------------
# Steps 9 to 12: changing modes. Two persistent clients driven through FIFOs —
# an operator (it creates the channel) and a plain member. See tests/it/kick.sh
# for why a FIFO rather than the `talk` helper.
# ---------------------------------------------------------------------------
OUTOP=$(mktemp)
OUTME=$(mktemp)
FIFOOP=$(mktemp -u)
FIFOME=$(mktemp -u)
mkfifo "$FIFOOP" "$FIFOME"

(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	cat "$FIFOOP" >&3 &
	cat <&3 > "$OUTOP"
) &
OP=$!
disown
exec 4>"$FIFOOP"
printf 'PASS secret\r\nNICK chefe\r\nUSER chefe 0 * :Chefe\r\nJOIN #sala\r\n' >&4
sleep 2

(
	exec 3<>/dev/tcp/127.0.0.1/"$PORT"
	cat "$FIFOME" >&3 &
	cat <&3 > "$OUTME"
) &
ME=$!
disown
exec 5>"$FIFOME"
printf 'PASS secret\r\nNICK membro\r\nUSER membro 0 * :Membro\r\nJOIN #sala\r\n' >&5
sleep 2

# A plain member cannot change a mode.
printf 'MODE #sala +i\r\n' >&5
sleep 1
check "membro comum alterando modo -> 482" \
	":ircserv 482 membro #sala :You're not channel operator" \
	"$(seen "$OUTME" | grep ' 482 ' | tail -1)"

# An unknown flag is 472, and does not stop the flags around it.
printf 'MODE #sala +zt\r\n' >&4
sleep 1
check "flag desconhecida -> 472" \
	":ircserv 472 chefe z :is unknown mode char to me" \
	"$(seen "$OUTOP" | grep ' 472 ' | tail -1)"
check "e a flag valida ao lado dela foi aplicada" \
	":chefe!chefe@127.0.0.1 MODE #sala +t" \
	"$(seen "$OUTOP" | grep ' MODE #sala ' | tail -1)"

# +t IS NOW REACHABLE END TO END — the case tests/it/topic.sh had to leave to
# the unit tests, because only MODE can set the mode.
printf 'TOPIC #sala :posso mudar?\r\n' >&5
sleep 1
check "sob +t, membro comum mudando topico -> 482" \
	":ircserv 482 membro #sala :You're not channel operator" \
	"$(seen "$OUTME" | grep ' 482 ' | tail -1)"

printf 'MODE #sala -t\r\n' >&4
sleep 1
printf 'TOPIC #sala :agora posso\r\n' >&5
sleep 1
check "sem +t, membro comum muda o topico" \
	":membro!membro@127.0.0.1 TOPIC #sala :agora posso" \
	"$(seen "$OUTME" | grep ' TOPIC #sala ' | tail -1)"

# +k, AND THE JOIN GATE OF STEP 2, end to end for the first time.
printf 'MODE #sala +k segredo\r\n' >&4
sleep 1
check "a chave aparece no 324 para um membro" \
	":ircserv 324 membro #sala +k segredo" \
	"$(printf 'MODE #sala\r\n' >&5; sleep 1; seen "$OUTME" | grep ' 324 ' | tail -1)"

check "entrar sem a chave -> 475" \
	"1" "$(talk "$REG"'JOIN #sala\r\n' | grep -c ' 475 alice #sala :Cannot join channel (+k)')"
check "entrar com a chave certa funciona" \
	"1" "$(talk "$REG"'JOIN #sala segredo\r\n' | grep -c ' 366 alice #sala ')"

printf 'MODE #sala -k\r\n' >&4
sleep 1
check "-k reabre o canal" \
	"1" "$(talk "$REG"'JOIN #sala\r\n' | grep -c ' 366 alice #sala ')"

# +i, and the invitation that opens it.
printf 'MODE #sala +i\r\n' >&4
sleep 1
check "entrar sem convite num canal +i -> 473" \
	"1" "$(talk "$REG"'JOIN #sala\r\n' | grep -c ' 473 alice #sala :Cannot join channel (+i)')"

# +l, counted against the members already in (chefe and membro make two).
printf 'MODE #sala -i+l 2\r\n' >&4
sleep 1
check "canal cheio -> 471" \
	"1" "$(talk "$REG"'JOIN #sala\r\n' | grep -c ' 471 alice #sala :Cannot join channel (+l)')"

#    D12: a limit that is not a positive number is ignored in silence, and the
#    old one stays in force.
printf 'MODE #sala +l abc\r\n' >&4
sleep 1
check "limite nao numerico e ignorado, sem numeric (D12)" \
	"1" "$(talk "$REG"'JOIN #sala\r\n' | grep -c ' 471 ')"

printf 'MODE #sala -l\r\n' >&4
sleep 1
check "-l libera a entrada" \
	"1" "$(talk "$REG"'JOIN #sala\r\n' | grep -c ' 366 alice #sala ')"

# +o, and the promoted member using the badge.
printf 'MODE #sala +o membro\r\n' >&4
sleep 1
check "+o e transmitido, nomeando o alvo" \
	":chefe!chefe@127.0.0.1 MODE #sala +o membro" \
	"$(seen "$OUTME" | grep ' MODE #sala +o ' | tail -1)"

printf 'MODE #sala +t\r\n' >&5
sleep 1
check "o novo operador consegue alterar modos" \
	":membro!membro@127.0.0.1 MODE #sala +t" \
	"$(seen "$OUTME" | grep ' MODE #sala +t' | tail -1)"

printf 'MODE #sala -o membro\r\n' >&4
sleep 1
printf 'MODE #sala -t\r\n' >&5
sleep 1
check "depois do -o, ele volta a levar 482" \
	":ircserv 482 membro #sala :You're not channel operator" \
	"$(seen "$OUTME" | grep ' 482 ' | tail -1)"

# Positional consumption: -k takes nothing, so the number belongs to +l.
printf 'MODE #sala +k umachave\r\n' >&4
sleep 1
printf 'MODE #sala -k+l 7\r\n' >&4
sleep 1
check "-k nao consome parametro, entao o numero vai para o +l" \
	":chefe!chefe@127.0.0.1 MODE #sala -k+l 7" \
	"$(seen "$OUTOP" | grep ' MODE #sala -k' | tail -1)"

exec 4>&- 2>/dev/null
exec 5>&- 2>/dev/null
kill -9 $(pgrep -P "$OP") "$OP" 2>/dev/null
kill -9 $(pgrep -P "$ME") "$ME" 2>/dev/null
rm -f "$OUTOP" "$OUTME" "$FIFOOP" "$FIFOME"

kill -0 "$SRV" 2>/dev/null && ALIVE=vivo || ALIVE=MORTO
check "servidor continua vivo no fim" "vivo" "$ALIVE"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
