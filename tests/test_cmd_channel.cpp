#include <string>

#include "Channel.hpp"
#include "Client.hpp"
#include "Command.hpp"
#include "Message.hpp"
#include "Server.hpp"

#include "harness.hpp"

// Unit tests for the channel command handlers — the DOMAIN half of the seam.
//
// Same technique as tests/test_cmd_registration.cpp: a handler is a free
// function taking (Server&, Client&, const Message&), so it is called directly
// here, with no dispatcher and no socket. Every assertion reads
// getOutputBuffer().
//
// WHAT THIS FILE CANNOT REACH is anything that needs Server::_clients, since
// clients only enter that map through accept(). findClientByNick therefore
// always returns NULL here, so the happy path of PRIVMSG <nick> and of INVITE
// belongs in tests/it/*.sh instead. KICK and MODE +o resolve their target by
// scanning the channel's member set (decision D10 in docs/FASE3.md), which is
// what keeps their happy paths testable at this level.

static void	feed(Server &server, Client &client, const std::string &line)
{
	Message	msg = parseMessage(line);

	if (msg.command == "MODE")
		cmdMode(server, client, msg);
}

static bool	contains(const std::string &haystack, const std::string &needle)
{
	return (haystack.find(needle) != std::string::npos);
}

// The minimal MODE of step 0.6: it answers the query and refuses nothing else.
// Changing modes arrives in steps 9 to 12.
static void	testModeErrors(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	client.setNickname("alice");
	client.setUsername("alice");

	feed(server, client, "MODE");
	checkEqual(client.getOutputBuffer(),
		":ircserv 461 alice MODE :Not enough parameters\r\n",
		"MODE with no parameter is 461");

	Client	missing(-1, "localhost");

	missing.setNickname("alice");
	feed(server, missing, "MODE #nope");
	checkEqual(missing.getOutputBuffer(),
		":ircserv 403 alice #nope :No such channel\r\n",
		"MODE on a channel that does not exist is 403");
}

// THE CASE THAT MADE THIS STEP EXIST. irssi 1.4.5 sends "MODE <nick> +i" on
// its own, about two seconds after registering, on every single connection and
// without joining anything — it was captured on the wire in step 0 of
// docs/FASE3.md.
//
// Answering 403 ":No such channel" to that would put an error line in the
// status window of everybody who connects, which is the exact trap decision D2
// fell into with CAP. Silence is measured to be correct here: with no reply at
// all, irssi's status window stays clean.
static void	testUserModeIsIgnoredSilently(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	client.setNickname("edu_k");

	feed(server, client, "MODE edu_k +i");
	checkEqual(client.getOutputBuffer(), "",
		"MODE on a nickname is ignored in silence — the real irssi case");

	feed(server, client, "MODE edu_k");
	checkEqual(client.getOutputBuffer(), "",
		"and so is a user-mode query, with no flags at all");

	// A target that is neither a nickname nor a valid channel is still not our
	// business: only names starting with '#' reach the channel path.
	feed(server, client, "MODE &foo +i");
	checkEqual(client.getOutputBuffer(), "",
		"a non-# target never reaches the channel code");

	check(!client.isDisconnecting(), "and none of that disconnects anybody");
}

static void	testModeQuery(void)
{
	Server	server(6667, "secret");
	Client	member(-1, "localhost");

	member.setNickname("alice");

	Channel	*channel = server.getOrCreateChannel("#room");

	channel->addMember(&member);

	// A brand new channel has no modes at all, and modeString still leads with
	// the '+' — "+" alone is the honest answer, not an empty string.
	feed(server, member, "MODE #room");
	checkEqual(member.getOutputBuffer(),
		":ircserv 324 alice #room +\r\n",
		"a channel with no modes answers 324 with a bare +");

	channel->setInviteOnly(true);
	channel->setTopicRestricted(true);
	channel->setKey("segredo");
	channel->setUserLimit(10);

	Client	insider(-1, "localhost");

	insider.setNickname("bob");
	channel->addMember(&insider);
	feed(server, insider, "MODE #room");
	checkEqual(insider.getOutputBuffer(),
		":ircserv 324 bob #room +itkl segredo 10\r\n",
		"a member sees the flags AND their parameters, key included");

	// THE KEY IS NOT PUBLIC. Handing it to a stranger would make +k pointless:
	// they could read it here and then walk straight into the channel.
	Client	outsider(-1, "localhost");

	outsider.setNickname("carol");
	feed(server, outsider, "MODE #room");
	checkEqual(outsider.getOutputBuffer(),
		":ircserv 324 carol #room +itkl\r\n",
		"a non-member sees which flags are set, but not the key");

	// The channel is looked up case-insensitively at the seam (decision D5),
	// and the reply carries the ORIGINAL spelling back.
	Client	shouting(-1, "localhost");

	shouting.setNickname("dave");
	feed(server, shouting, "MODE #ROOM");
	check(contains(shouting.getOutputBuffer(), " 324 dave #room "),
		"the lookup ignores case and the reply keeps the original spelling");
}

// Changing modes is steps 9 to 12. Until then a change request must not be
// answered with a guess: no 472, which means "unknown flag", and no 482, which
// claims a permission decision this step does not yet know how to make.
static void	testModeChangeIsNotImplementedYet(void)
{
	Server	server(6667, "secret");
	Client	member(-1, "localhost");

	member.setNickname("alice");

	Channel	*channel = server.getOrCreateChannel("#room");

	channel->addMember(&member);
	channel->addOperator(&member);

	feed(server, member, "MODE #room +i");
	checkEqual(member.getOutputBuffer(), "",
		"a mode CHANGE is silent for now — steps 9 to 12 implement it");
	check(!channel->isInviteOnly(),
		"and it changes nothing yet");
}

void	runCommandChannelTests(void)
{
	testModeErrors();
	testUserModeIsIgnoredSilently();
	testModeQuery();
	testModeChangeIsNotImplementedYet();
}
