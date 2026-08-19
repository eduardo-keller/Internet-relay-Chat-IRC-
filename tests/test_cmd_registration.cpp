#include <string>

#include "Client.hpp"
#include "Command.hpp"
#include "Limits.hpp"
#include "Message.hpp"
#include "Server.hpp"

#include "harness.hpp"

// Unit tests for the registration state machine: PASS, NICK, USER and the
// 001-004 burst.
//
// A command handler is a free function taking (Server&, Client&, const
// Message&), so it can be called DIRECTLY here — no dispatcher, no socket, no
// registration gate. That is not a trick, it is the seam working: the gate
// lives in the dispatcher, so a handler is a pure function of the objects
// handed to it, and every assertion below reads getOutputBuffer().
//
// One case is deliberately absent: 433, the duplicate nickname. It needs two
// clients inside Server::_clients, and clients only get in there through
// accept(). Opening a back door in the seam to reach it would damage the thing
// being tested, so it is proved with two live connections in
// tests/it/registration.sh instead — better evidence anyway.

static void	feed(Server &server, Client &client, const std::string &line)
{
	Message	msg = parseMessage(line);

	if (msg.command == "PASS")
		cmdPass(server, client, msg);
	else if (msg.command == "NICK")
		cmdNick(server, client, msg);
	else if (msg.command == "USER")
		cmdUser(server, client, msg);
}

static bool	contains(const std::string &haystack, const std::string &needle)
{
	return (haystack.find(needle) != std::string::npos);
}

static int	countOf(const std::string &haystack, const std::string &needle)
{
	int						count = 0;
	std::string::size_type	pos = haystack.find(needle);

	while (pos != std::string::npos)
	{
		++count;
		pos = haystack.find(needle, pos + needle.size());
	}
	return (count);
}

static void	testPass(void)
{
	Server	server(6667, "secret");
	Client	noParams(-1, "localhost");

	feed(server, noParams, "PASS");
	checkEqual(noParams.getOutputBuffer(),
		":ircserv 461 * PASS :Not enough parameters\r\n",
		"PASS without a password is 461, and the target is * before a nick");

	Client	wrong(-1, "localhost");

	feed(server, wrong, "PASS wrongpassword");
	check(contains(wrong.getOutputBuffer(), " 464 * :Password incorrect"),
		"a wrong password is 464");
	check(wrong.isDisconnecting(),
		"and the client is marked for disconnection");
	check(contains(wrong.getOutputBuffer(), "ERROR :Password incorrect"),
		"with the ERROR queued behind it, so the reap can flush both");
	check(!wrong.hasPass(), "a wrong password sets no flag");

	Client	right(-1, "localhost");

	feed(server, right, "PASS secret");
	checkEqual(right.getOutputBuffer(), "",
		"the correct password is accepted in silence");
	check(right.hasPass(), "and sets the flag");
}

static void	testNickAndUserNeedPassFirst(void)
{
	Server	server(6667, "secret");
	Client	nickFirst(-1, "localhost");

	// RFC 2812 section 3.1.1 requires the password before any registration
	// attempt. This 451 comes from the handler, NOT from the dispatcher's
	// gate — NICK is one of the six commands that gate lets through.
	feed(server, nickFirst, "NICK alice");
	checkEqual(nickFirst.getOutputBuffer(),
		":ircserv 451 * :You have not registered\r\n",
		"NICK before PASS is 451");
	check(nickFirst.getNickname().empty(), "and the nickname is not set");

	Client	userFirst(-1, "localhost");

	feed(server, userFirst, "USER alice 0 * :Alice");
	checkEqual(userFirst.getOutputBuffer(),
		":ircserv 451 * :You have not registered\r\n",
		"USER before PASS is 451 too");
}

static void	testNickValidation(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	feed(server, client, "PASS secret");

	feed(server, client, "NICK");
	check(contains(client.getOutputBuffer(), " 431 * :No nickname given"),
		"NICK with no parameter is 431");

	// '~' is NOT among RFC 2812's nickname specials, which are []\\^_`{|}.
	feed(server, client, "NICK ~foo");
	check(contains(client.getOutputBuffer(), " 432 * ~foo :Erroneous nickname"),
		"a nickname starting with ~ is 432");

	feed(server, client, "NICK 4lice");
	check(contains(client.getOutputBuffer(), " 432 * 4lice :Erroneous nickname"),
		"a nickname starting with a digit is 432");

	feed(server, client, "NICK " + std::string(irc::MAX_NICKNAME_LEN + 1, 'a'));
	check(countOf(client.getOutputBuffer(), " 432 ") == 3,
		"a nickname over MAX_NICKNAME_LEN is 432");
	check(client.getNickname().empty(),
		"none of the rejected nicknames was stored");

	feed(server, client, "NICK alice");
	checkEqual(client.getNickname(), "alice", "a valid nickname is accepted");
	check(client.hasNick(), "and flips the flag");
	check(!client.isRegistered(), "but NICK alone does not register anybody");
}

static void	testWelcomeBurst(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	feed(server, client, "PASS secret");
	feed(server, client, "NICK alice");
	checkEqual(client.getOutputBuffer(), "",
		"nothing is sent until USER completes the set");

	feed(server, client, "USER alice 0 * :Alice Liddell");
	check(client.isRegistered(), "PASS + NICK + USER registers the client");

	const std::string	&out = client.getOutputBuffer();

	check(contains(out, ":ircserv 001 alice :Welcome to the Internet Relay "
			"Network alice!alice@localhost"),
		"001 carries the full nick!user@host prefix");
	check(contains(out, ":ircserv 002 alice :Your host is ircserv, running "
			"version 1.0"),
		"002 names the server and version");
	check(contains(out, ":ircserv 003 alice :This server was created "),
		"003 reports a creation date");
	check(contains(out, ":ircserv 004 alice ircserv 1.0 - itkol"),
		"004 is the D3 payload: no user modes, five channel modes");

	// THE ORDER IS PART OF THE CONTRACT: irssi reads 001 as "connected".
	check(out.find(" 001 ") < out.find(" 002 ")
		&& out.find(" 002 ") < out.find(" 003 ")
		&& out.find(" 003 ") < out.find(" 004 "),
		"the burst arrives in the order 001 002 003 004");

	// And the target switched from * to the nickname now that there is one.
	check(!contains(out, " 001 * "), "the target is the nick, not * ");
}

static void	testBurstFiresExactlyOnce(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	feed(server, client, "PASS secret");
	feed(server, client, "NICK alice");
	feed(server, client, "USER alice 0 * :Alice");
	check(countOf(client.getOutputBuffer(), " 001 ") == 1,
		"the welcome fires once");

	// A second NICK after registration is a nick CHANGE, not a new
	// registration: it must broadcast, never replay the welcome.
	feed(server, client, "NICK bob");
	check(countOf(client.getOutputBuffer(), " 001 ") == 1,
		"changing nick afterwards does not replay the welcome");
	check(contains(client.getOutputBuffer(),
			":alice!alice@localhost NICK :bob"),
		"the change is announced with the OLD prefix, echoed to the origin");
	checkEqual(client.getNickname(), "bob", "and the new nickname is stored");
}

static void	testAlreadyRegistered(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	feed(server, client, "PASS secret");
	feed(server, client, "NICK alice");
	feed(server, client, "USER alice 0 * :Alice");

	Client	other(-1, "localhost");

	feed(server, other, "PASS secret");
	feed(server, other, "NICK bob");
	feed(server, other, "USER bob 0 * :Bob");

	feed(server, client, "PASS secret");
	check(contains(client.getOutputBuffer(),
			" 462 alice :Unauthorized command (already registered)"),
		"a second PASS after registration is 462");

	feed(server, other, "USER bob 0 * :Bob");
	check(contains(other.getOutputBuffer(),
			" 462 bob :Unauthorized command (already registered)"),
		"a second USER after registration is 462");
}

static void	testUserParameters(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	feed(server, client, "PASS secret");
	feed(server, client, "NICK alice");

	feed(server, client, "USER alice 0 *");
	check(contains(client.getOutputBuffer(),
			" 461 alice USER :Not enough parameters"),
		"USER with three parameters is 461");
	check(!client.isRegistered(), "and does not register");

	feed(server, client, "USER myuser 0 * :My Real Name");
	checkEqual(client.getUsername(), "myuser", "USER stores the username");
	checkEqual(client.getRealname(), "My Real Name",
		"and the realname from the trailing parameter, spaces included");
}

void	runCommandRegistrationTests(void)
{
	testPass();
	testNickAndUserNeedPassFirst();
	testNickValidation();
	testWelcomeBurst();
	testBurstFiresExactlyOnce();
	testAlreadyRegistered();
	testUserParameters();
}
