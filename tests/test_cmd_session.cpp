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

static void	testPongAndCapAreSilent(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	feed(server, client, "PONG ircserv");
	checkEqual(client.getOutputBuffer(), "",
		"PONG is accepted and ignored: this server sends no PING to answer");

	feed(server, client, "CAP LS 302");
	checkEqual(client.getOutputBuffer(), "",
		"CAP is silent (D2) — a 421 here would show as an error in irssi");
	check(!client.isDisconnecting(), "and neither of them disconnects anybody");
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

void	runCommandSessionTests(void)
{
	testPing();
	testPongAndCapAreSilent();
	testQuit();
}
