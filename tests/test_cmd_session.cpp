#include <string>

#include "Client.hpp"
#include "Command.hpp"
#include "Message.hpp"
#include "Server.hpp"

#include "harness.hpp"

// Unit tests for the session commands: PING, PONG, QUIT and CAP.
//
// None of the four is in the subject's command list. They exist because the
// subject requires the reference client to connect without encountering any
// error, and irssi opens with CAP, pings on a timer, and sends QUIT on /quit.
//
// What these tests CANNOT show is the reason cmdQuit is written the way it is:
// that a QUIT sharing a packet with another command must not leave the drain
// loop holding a freed Client. That needs a real socket and a real poll
// iteration, and it lives in tests/it/session.sh, under valgrind.

static void	feed(Server &server, Client &client, const std::string &line)
{
	Message	msg = parseMessage(line);

	if (msg.command == "PING")
		cmdPing(server, client, msg);
	else if (msg.command == "PONG")
		cmdPong(server, client, msg);
	else if (msg.command == "QUIT")
		cmdQuit(server, client, msg);
	else if (msg.command == "CAP")
		cmdCap(server, client, msg);
	else if (msg.command == "WHO")
		cmdWho(server, client, msg);
	else if (msg.command == "WHOIS")
		cmdWhois(server, client, msg);
}

static void	testPing(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	feed(server, client, "PING token123");
	checkEqual(client.getOutputBuffer(),
		":ircserv PONG ircserv :token123\r\n",
		"PING echoes the token in a server-prefixed PONG");

	// The token can arrive as a trailing parameter, spaces and all — irssi
	// sends "PING :irc.example.net" style tokens.
	Client	trailing(-1, "localhost");

	feed(server, trailing, "PING :two words");
	checkEqual(trailing.getOutputBuffer(),
		":ircserv PONG ircserv :two words\r\n",
		"a trailing token keeps its spaces");

	Client	empty(-1, "localhost");

	feed(server, empty, "PING");
	checkEqual(empty.getOutputBuffer(),
		":ircserv 461 * PING :Not enough parameters\r\n",
		"PING with no token is 461, not an invented 409");

	// PING is one of the six commands allowed before registration, so the
	// handler must work on a client with no nickname at all.
	Client	unregistered(-1, "localhost");

	feed(server, unregistered, "PING early");
	check(unregistered.getOutputBuffer() == ":ircserv PONG ircserv :early\r\n",
		"PING works before registration");
}

static void	testPongIsSilent(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	feed(server, client, "PONG ircserv");
	checkEqual(client.getOutputBuffer(), "",
		"PONG is accepted and ignored: this server sends no PING to answer");
	check(!client.isDisconnecting(), "and it disconnects nobody");
}

// CAP HAS TO ANSWER. Decision D17 in docs/FASE3.md, which supersedes D2.
//
// This test replaced one that asserted the exact opposite, and the reason is
// worth keeping: D2 made cmdCap a silent no-op to keep a 421 out of irssi's
// status window, and every test we wrote for it passed. Then irssi 1.4.5 was
// pointed at the server for the first time and printed
// "Waiting for CAP LS response..." forever — it never sent PASS, NICK or USER,
// so nobody could register at all. The old assertion was green the whole time.
//
// That is ARCHITECTURE.md section 9 happening to us: unit tests prove we are
// internally consistent, not that we are correct.
static void	testCapNegotiation(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	// An EMPTY capability list. We implement no capabilities, and this is how
	// you say so — irssi answers it with CAP END and proceeds to register.
	// The target is "*" because a client negotiating CAP has no nickname yet.
	feed(server, client, "CAP LS 302");
	checkEqual(client.getOutputBuffer(), ":ircserv CAP * LS :\r\n",
		"CAP LS 302 is answered with an empty capability list");

	// The version argument is optional; older clients send a bare LS.
	Client	bare(-1, "localhost");

	feed(server, bare, "CAP LS");
	checkEqual(bare.getOutputBuffer(), ":ircserv CAP * LS :\r\n",
		"a bare CAP LS gets the same answer");

	// Nothing was offered, so nothing can be granted. NAK is the honest reply
	// and the client carries on regardless.
	Client	requester(-1, "localhost");

	feed(server, requester, "CAP REQ :multi-prefix sasl");
	checkEqual(requester.getOutputBuffer(),
		":ircserv CAP * NAK :multi-prefix sasl\r\n",
		"CAP REQ is refused with NAK, echoing what was asked for");

	// END closes the negotiation. There is nothing to say back, and saying
	// something here would be noise in front of the welcome burst.
	Client	ending(-1, "localhost");

	feed(server, ending, "CAP END");
	checkEqual(ending.getOutputBuffer(), "",
		"CAP END is silent");

	// The SUBCOMMAND is matched case-insensitively. irssi sends it uppercase,
	// but a lenient comparison costs one helper and removes a way to hang.
	//
	// The command NAME is deliberately still "CAP" here: making that half
	// case-insensitive is the dispatcher's job, not this handler's, and feed()
	// above is not the dispatcher — it matches msg.command literally. That
	// half is covered over a real socket in tests/it/dispatch.sh.
	Client	lower(-1, "localhost");

	feed(server, lower, "CAP ls");
	checkEqual(lower.getOutputBuffer(), ":ircserv CAP * LS :\r\n",
		"the subcommand is case-insensitive");

	// Neither of these may crash or reply. A bare CAP is the one that would
	// index params[0] on an empty vector.
	Client	empty(-1, "localhost");

	feed(server, empty, "CAP");
	checkEqual(empty.getOutputBuffer(), "",
		"CAP with no subcommand is silent, and does not read past the params");

	Client	unknown(-1, "localhost");

	feed(server, unknown, "CAP NONSENSE");
	checkEqual(unknown.getOutputBuffer(), "",
		"an unknown subcommand is silent");
	check(!unknown.isDisconnecting(), "and CAP never disconnects anybody");
}

static void	testQuit(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	feed(server, client, "QUIT :goodbye everyone");
	check(client.isDisconnecting(), "QUIT marks the client");
	checkEqual(client.getQuitReason(), "goodbye everyone",
		"and records the reason, trailing spaces included");
	checkEqual(client.getOutputBuffer(), "ERROR :goodbye everyone\r\n",
		"the ERROR is queued for the reap to flush before close()");

	// THE CLIENT IS STILL A VALID OBJECT. Nothing was deleted here; that
	// happens in reapDisconnected, at the end of the poll iteration.
	checkEqual(client.getHostname(), "localhost",
		"the Client& handed to the handler stays valid after QUIT");

	Client	noReason(-1, "localhost");

	feed(server, noReason, "QUIT");
	checkEqual(noReason.getQuitReason(), "Client quit",
		"a QUIT with no parameter gets a default reason");

	// A second QUIT in the same packet must not queue a second ERROR — the
	// guard in disconnectClient is what stops that turning into recursion on
	// a client whose queue is full.
	feed(server, noReason, "QUIT :again");
	checkEqual(noReason.getQuitReason(), "Client quit",
		"a second QUIT does not overwrite the first reason");
	checkEqual(noReason.getOutputBuffer(), "ERROR :Client quit\r\n",
		"and does not queue a second ERROR");
}

// WHO AND WHOIS ARE ACCEPTED AND IGNORED, and the reason is the CAP lesson a
// second time. Neither is in the subject's command list, and this server has no
// user database to answer them from — but irssi 1.4.5 sends "WHO <channel>"
// and "WHOIS <nick>" ON ITS OWN as soon as there is a second person in a
// channel, and 421 :Unknown command puts an error line in the status window of
// both users. The subject requires the reference client to connect without
// encountering any error.
//
// Measured in phase 3 step 1: this only shows up with TWO clients. The single
// -client capture of step 0 never triggered it, which is exactly why decision
// D15 was wrong the first time it was written down.
//
// Silence is the whole implementation. Answering with an invented 352 or 315
// would mean putting numerics in the table that nothing else uses, and the
// client shows nothing either way.
static void	testWhoAndWhoisAreSilent(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	client.setNickname("alice");

	feed(server, client, "WHO #room");
	checkEqual(client.getOutputBuffer(), "",
		"WHO is accepted and ignored — irssi sends it unprompted");

	feed(server, client, "WHOIS bob");
	checkEqual(client.getOutputBuffer(), "",
		"WHOIS likewise");

	// No parameter must not read past the vector, same trap as a bare CAP.
	feed(server, client, "WHO");
	feed(server, client, "WHOIS");
	checkEqual(client.getOutputBuffer(), "",
		"neither reads past its parameters when given none");
	check(!client.isDisconnecting(), "and neither disconnects anybody");
}

void	runCommandSessionTests(void)
{
	testPing();
	testPongIsSilent();
	testCapNegotiation();
	testWhoAndWhoisAreSilent();
	testQuit();
}
