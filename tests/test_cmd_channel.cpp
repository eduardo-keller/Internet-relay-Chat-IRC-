#include <string>

#include "Channel.hpp"
#include "Client.hpp"
#include "Command.hpp"
#include "Limits.hpp"
#include "Message.hpp"
#include "Server.hpp"
#include "Utils.hpp"

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
	else if (msg.command == "JOIN")
		cmdJoin(server, client, msg);
	else if (msg.command == "PART")
		cmdPart(server, client, msg);
	else if (msg.command == "PRIVMSG")
		cmdPrivmsg(server, client, msg);
	else if (msg.command == "TOPIC")
		cmdTopic(server, client, msg);
	else if (msg.command == "KICK")
		cmdKick(server, client, msg);
	else if (msg.command == "INVITE")
		cmdInvite(server, client, msg);
}

static bool	contains(const std::string &haystack, const std::string &needle)
{
	return (haystack.find(needle) != std::string::npos);
}

// How many times `needle` appears in `haystack`. "Exactly once" is the
// assertion that matters for a nick inside a NAMES reply; "at least once"
// would pass even when the name is duplicated across two lines.
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

static Client	*makeUser(Client &c, const std::string &nick)
{
	c.setNickname(nick);
	c.setUsername(nick);
	return (&c);
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

// --- steps 9 to 12: changing modes ---------------------------------------

static void	testModePrivilegeAndUnknownFlag(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");
	Client	bob(-1, "localhost");

	makeUser(alice, "alice");
	makeUser(bob, "bob");
	feed(server, alice, "JOIN #room");
	feed(server, bob, "JOIN #room");

	Channel	*channel = server.findChannel("#room");

	feed(server, bob, "MODE #room +i");
	check(contains(bob.getOutputBuffer(),
			" 482 bob #room :You're not channel operator"),
		"a plain member changing a mode is 482");
	check(!channel->isInviteOnly(), "and nothing changed");

	feed(server, alice, "MODE #room +z");
	check(contains(alice.getOutputBuffer(),
			" 472 alice z :is unknown mode char to me"),
		"a flag outside itkol is 472");

	// AN UNKNOWN FLAG DOES NOT ABORT THE REST OF THE STRING. The client asked
	// for two things; refusing the one we understand because of the one we do
	// not is worse service than the error alone.
	feed(server, alice, "MODE #room +zi");
	check(channel->isInviteOnly(),
		"the flags around an unknown one are still applied");
}

static void	testModeInviteOnlyAndTopicLock(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");
	Client	bob(-1, "localhost");

	makeUser(alice, "alice");
	makeUser(bob, "bob");
	feed(server, alice, "JOIN #room");
	feed(server, bob, "JOIN #room");

	Channel	*channel = server.findChannel("#room");

	feed(server, alice, "MODE #room +i");
	check(channel->isInviteOnly(), "+i sets invite-only");
	check(contains(bob.getOutputBuffer(),
			":alice!alice@localhost MODE #room +i\r\n"),
		"and the change is broadcast to the channel");
	check(contains(alice.getOutputBuffer(),
			":alice!alice@localhost MODE #room +i\r\n"),
		"including back to the operator who made it");

	feed(server, alice, "MODE #room -i");
	check(!channel->isInviteOnly(), "-i clears it");

	feed(server, alice, "MODE #room +t");
	check(channel->isTopicRestricted(), "+t locks the topic to operators");
	feed(server, alice, "MODE #room -t");
	check(!channel->isTopicRestricted(), "-t unlocks it");

	// SEVERAL FLAGS IN ONE STRING, and one broadcast for the lot.
	const std::string	before = bob.getOutputBuffer();

	feed(server, alice, "MODE #room +it");
	check(channel->isInviteOnly() && channel->isTopicRestricted(),
		"+it sets both");
	check(contains(bob.getOutputBuffer().substr(before.size()),
			"MODE #room +it"),
		"and both travel in a single MODE line");

	// The sign changes mid-string, and the line reports what happened.
	const std::string	before2 = bob.getOutputBuffer();

	feed(server, alice, "MODE #room -i+t");
	check(!channel->isInviteOnly() && channel->isTopicRestricted(),
		"a sign change mid-string is honoured");
	check(contains(bob.getOutputBuffer().substr(before2.size()),
			"MODE #room -i+t"),
		"and the broadcast carries both signs");
}

// THE REGRESSION THAT CLOSES THE CIRCLE WITH STEP 2. Until now +i and +l were
// only ever set through the Channel API in a test. Now MODE sets them, and the
// JOIN gates have to answer to that.
static void	testModeThenJoinGate(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");

	makeUser(alice, "alice");
	feed(server, alice, "JOIN #room");
	feed(server, alice, "MODE #room +i");

	Client	stranger(-1, "localhost");

	makeUser(stranger, "stranger");
	feed(server, stranger, "JOIN #room");
	check(contains(stranger.getOutputBuffer(), " 473 stranger #room "),
		"a channel made +i by MODE refuses an uninvited JOIN");

	feed(server, alice, "INVITE stranger #room");
	server.findChannel("#room")->addInvite(&stranger);
	feed(server, stranger, "JOIN #room");
	check(server.findChannel("#room")->isMember(&stranger),
		"and lets an invited one in");
}

static void	testModeKey(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");

	makeUser(alice, "alice");
	feed(server, alice, "JOIN #room");

	Channel	*channel = server.findChannel("#room");

	// +k WITHOUT A PARAMETER IS 461. There is no key to set, and picking one
	// would be inventing a password for a channel.
	feed(server, alice, "MODE #room +k");
	check(contains(alice.getOutputBuffer(),
			" 461 alice MODE :Not enough parameters"),
		"+k with no key is 461");
	check(!channel->hasKey(), "and no key is set");

	feed(server, alice, "MODE #room +k segredo");
	check(channel->hasKey() && channel->getKey() == "segredo",
		"+k sets the key");

	Client	stranger(-1, "localhost");

	makeUser(stranger, "stranger");
	feed(server, stranger, "JOIN #room");
	check(contains(stranger.getOutputBuffer(), " 475 stranger #room "),
		"and the JOIN gate starts refusing without it");

	// -k TAKES NO PARAMETER. Removing a key does not require producing it.
	feed(server, alice, "MODE #room -k");
	check(!channel->hasKey(), "-k clears the key with no parameter");
	feed(server, stranger, "JOIN #room");
	check(channel->isMember(&stranger), "and the channel is open again");
}

static void	testModeLimit(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");

	makeUser(alice, "alice");
	feed(server, alice, "JOIN #room");

	Channel	*channel = server.findChannel("#room");

	feed(server, alice, "MODE #room +l");
	check(contains(alice.getOutputBuffer(),
			" 461 alice MODE :Not enough parameters"),
		"+l with no number is 461");

	feed(server, alice, "MODE #room +l 2");
	check(channel->hasUserLimit() && channel->getUserLimit() == 2,
		"+l sets the limit");

	// D12: A PARAMETER THAT IS PRESENT BUT ABSURD IS IGNORED IN SILENCE. There
	// is no numeric in the table for "that is not a number", and inventing one
	// is forbidden by ARCHITECTURE.md section 10.
	feed(server, alice, "MODE #room +l abc");
	check(channel->getUserLimit() == 2, "a non-numeric limit is ignored");
	feed(server, alice, "MODE #room +l 0");
	check(channel->getUserLimit() == 2, "and so is zero");
	feed(server, alice, "MODE #room +l -5");
	check(channel->getUserLimit() == 2, "and so is a negative number");

	Client	second(-1, "localhost");
	Client	third(-1, "localhost");

	makeUser(second, "second");
	makeUser(third, "third");
	feed(server, second, "JOIN #room");
	feed(server, third, "JOIN #room");
	check(contains(third.getOutputBuffer(), " 471 third #room "),
		"the third client meets the limit of two");

	feed(server, alice, "MODE #room -l");
	check(!channel->hasUserLimit(), "-l removes it with no parameter");
	feed(server, third, "JOIN #room");
	check(channel->isMember(&third), "and the queue moves");
}

static void	testModeOperator(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");
	Client	bob(-1, "localhost");

	makeUser(alice, "alice");
	makeUser(bob, "bob");
	feed(server, alice, "JOIN #room");
	feed(server, bob, "JOIN #room");

	Channel	*channel = server.findChannel("#room");

	feed(server, alice, "MODE #room +o");
	check(contains(alice.getOutputBuffer(),
			" 461 alice MODE :Not enough parameters"),
		"+o with no nickname is 461");

	// 441, NOT 401 — decision D10 again. The target is resolved by scanning
	// the channel's members, which is both the right question and what keeps
	// this testable without a socket.
	feed(server, alice, "MODE #room +o ninguem");
	check(contains(alice.getOutputBuffer(),
			" 441 alice ninguem #room :They aren't on that channel"),
		"promoting somebody who is not in the channel is 441");

	feed(server, alice, "MODE #room +o bob");
	check(channel->isOperator(&bob), "+o promotes a member");
	check(contains(bob.getOutputBuffer(),
			":alice!alice@localhost MODE #room +o bob\r\n"),
		"and the promotion is broadcast, naming the target");

	// The new operator can now do operator things.
	feed(server, bob, "MODE #room +t");
	check(channel->isTopicRestricted(), "and the new operator can use it");

	feed(server, alice, "MODE #room -o bob");
	check(!channel->isOperator(&bob), "-o demotes");
	feed(server, bob, "MODE #room -t");
	check(channel->isTopicRestricted(), "and the demoted member is refused");

	// AN OPERATOR MAY DEMOTE THEMSELVES, and the channel is then left with no
	// operator at all — the same outcome D20 accepts when one leaves.
	feed(server, alice, "MODE #room -o alice");
	check(!channel->isOperator(&alice), "an operator can stand down");
}

// PARAMETERS ARE CONSUMED LEFT TO RIGHT, AND ONLY BY THE FLAGS THAT TAKE ONE.
// This is where a mode parser usually goes wrong, so it gets its own test.
static void	testModeParameterConsumption(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");
	Client	bob(-1, "localhost");

	makeUser(alice, "alice");
	makeUser(bob, "bob");
	feed(server, alice, "JOIN #room");
	feed(server, bob, "JOIN #room");

	Channel	*channel = server.findChannel("#room");

	feed(server, alice, "MODE #room +kl segredo 10");
	check(channel->getKey() == "segredo" && channel->getUserLimit() == 10,
		"+kl takes two parameters, in order");

	// -k TAKES NONE, so the number after it belongs to +l and not to -k.
	feed(server, alice, "MODE #room -k+l 25");
	check(!channel->hasKey() && channel->getUserLimit() == 25,
		"-k consumes nothing, so the parameter goes to +l");

	feed(server, alice, "MODE #room +ko outra bob");
	check(channel->getKey() == "outra" && channel->isOperator(&bob),
		"+ko takes a key and then a nickname");

	// A flag that needs a parameter with none left is 461, and the flags
	// before it still applied.
	feed(server, alice, "MODE #room -o+k bob");
	check(!channel->isOperator(&bob),
		"-o consumed the nickname");
	check(contains(alice.getOutputBuffer(),
			" 461 alice MODE :Not enough parameters"),
		"and +k, left with nothing, is 461");
}

static void	testJoinErrors(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");

	makeUser(alice, "alice");

	feed(server, alice, "JOIN");
	checkEqual(alice.getOutputBuffer(),
		":ircserv 461 alice JOIN :Not enough parameters\r\n",
		"JOIN with no parameter at all is 461");

	// '&' is a valid channel prefix in RFC 2812 and deliberately not one of
	// ours (ARCHITECTURE.md section 5): every channel here is already
	// server-local, so it would be a second prefix with identical behaviour.
	Client	bob(-1, "localhost");

	makeUser(bob, "bob");
	feed(server, bob, "JOIN &foo");
	checkEqual(bob.getOutputBuffer(),
		":ircserv 403 bob &foo :No such channel\r\n",
		"a & channel is 403");

	Client	carol(-1, "localhost");

	makeUser(carol, "carol");
	feed(server, carol, "JOIN nohash");
	check(contains(carol.getOutputBuffer(), " 403 carol nohash "),
		"a name with no # is 403");

	feed(server, carol, "JOIN #");
	check(contains(carol.getOutputBuffer(), " 403 carol # "),
		"a bare # is too short to be a channel");

	check(server.findChannel("&foo") == NULL,
		"and none of the rejected names created a channel");
}

// THE CASE THAT COSTS NOTHING TO GET WRONG AND SHOWS UP ON EVERY CONNECT.
// irssi 1.4.5 sends "JOIN :" — one empty parameter — unprompted, before it has
// even sent CAP END. Confirmed on the wire against our server AND against a
// mock, with a pristine irssi profile, so it is not a leftover configuration.
//
// 403 or 461 there would put an error line in the status window of everybody
// who connects, which is what the subject forbids. Decision D18 in
// docs/FASE3.md. Note the asymmetry with testJoinErrors above: JOIN with NO
// parameter is still 461, because no client sends that — it is a typo at an nc
// prompt.
static void	testJoinWithEmptyChannelListIsSilent(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");

	makeUser(alice, "alice");

	Message	msg = parseMessage("JOIN :");

	check(msg.params.size() == 1 && msg.params[0].empty() && msg.hasTrailing,
		"the parser keeps an empty trailing parameter, which is what D18 needs");

	cmdJoin(server, alice, msg);
	checkEqual(alice.getOutputBuffer(), "",
		"JOIN with an empty channel list is silent — the real irssi case");
	check(!alice.isDisconnecting(), "and disconnects nobody");
}

static void	testJoinCreatesChannel(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");

	makeUser(alice, "alice");
	feed(server, alice, "JOIN #room");

	Channel	*channel = server.findChannel("#room");

	check(channel != NULL, "JOIN creates the channel when it does not exist");
	check(channel != NULL && channel->isMember(&alice),
		"and puts the sender in it");
	check(channel != NULL && channel->isOperator(&alice),
		"the first member of a new channel becomes its operator");

	const std::string	&out = alice.getOutputBuffer();

	check(contains(out, ":alice!alice@localhost JOIN #room\r\n"),
		"the JOIN is echoed back to the sender, with their own prefix");
	check(contains(out, ":ircserv 331 alice #room :No topic is set\r\n"),
		"a channel with no topic answers 331");
	check(contains(out, ":ircserv 353 alice = #room :@alice\r\n"),
		"353 lists the sender, prefixed with @ because they are the operator");
	check(contains(out, ":ircserv 366 alice #room :End of /NAMES list\r\n"),
		"366 closes the list, with the /NAMES spelling of decision D9");

	// THE ORDER IS THE CONTRACT (ARCHITECTURE.md section 6). Getting it wrong
	// is the usual reason a channel window opens empty in irssi.
	check(out.find(" JOIN #room") < out.find(" 331 ")
		&& out.find(" 331 ") < out.find(" 353 ")
		&& out.find(" 353 ") < out.find(" 366 "),
		"the sequence is JOIN, then 331/332, then 353, then 366");
}

static void	testJoinExistingChannel(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");
	Client	bob(-1, "localhost");

	makeUser(alice, "alice");
	makeUser(bob, "bob");

	feed(server, alice, "JOIN #room");

	Channel	*channel = server.findChannel("#room");

	channel->setTopic("o assunto do dia");
	feed(server, bob, "JOIN #room");

	check(channel->isMember(&bob), "the second client joins the same channel");
	check(!channel->isOperator(&bob),
		"but does NOT become an operator — only the creator does");

	const std::string	&bobOut = bob.getOutputBuffer();

	check(contains(bobOut, ":ircserv 332 bob #room :o assunto do dia\r\n"),
		"a channel WITH a topic answers 332 instead of 331");
	check(countOf(bobOut, "alice") >= 1 && countOf(bobOut, "bob") >= 1,
		"353 lists both members");
	check(contains(bobOut, "@alice"),
		"the operator carries the @ prefix");
	check(!contains(bobOut, "@bob"),
		"and a plain member does not");

	// THE JOIN IS A BROADCAST, not a reply: everyone already in the channel
	// has to see the new arrival, or their nick lists go stale.
	check(contains(alice.getOutputBuffer(), ":bob!bob@localhost JOIN #room\r\n"),
		"the client already in the channel is told about the new one");
	check(countOf(alice.getOutputBuffer(), " 353 ") == 1,
		"but alice does not get a second NAMES list — that is bob's reply");
}

static void	testJoinIsIdempotentAndCaseInsensitive(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");

	makeUser(alice, "alice");
	feed(server, alice, "JOIN #room");

	const std::string	firstPass = alice.getOutputBuffer();

	// Decision D5: _channels is keyed by utils::toIrcLower, so #Room and #room
	// are the same channel. Without that, a second one is created in silence
	// and the two users sit in rooms that look empty.
	feed(server, alice, "JOIN #Room");
	checkEqual(alice.getOutputBuffer(), firstPass,
		"re-joining a channel already joined says nothing, in any case");
	check(server.findChannel("#ROOM") == server.findChannel("#room"),
		"and did not create a second channel under another spelling");
	check(server.findChannel("#room")->memberCount() == 1,
		"nor a duplicate membership");
}

// RPL_NAMREPLY HAS TO SPLIT. One 353 line is capped at 510 bytes like every
// other message, and sendToClient truncates anything longer — which would drop
// members from the list in silence. Real servers send as many 353 lines as it
// takes, and so do we.
//
// A plain C array, not a std::vector: Channel stores non-owning Client*, and a
// vector that reallocates on growth would leave every one of them dangling
// (FASE2.md section 3.3, item 6).
static void	testNamesReplySplitsOverManyLines(void)
{
	Server	server(6667, "secret");
	Client	crowd[30];
	int		i;

	// The numbering starts at 10 so that every nickname is exactly 19
	// characters. With "…_1" and "…_10" in the same list, counting occurrences
	// of the shorter one would find the longer one too, and the "exactly once"
	// assertion below would be measuring nothing.
	for (i = 0; i < 30; ++i)
	{
		crowd[i].setNickname("verylongnickname_" + utils::toString(i + 10));
		crowd[i].setUsername("u");
		feed(server, crowd[i], "JOIN #crowded");
	}

	const std::string	&out = crowd[29].getOutputBuffer();

	check(countOf(out, " 353 ") > 1,
		"a crowded channel needs more than one 353 line");

	// ONLY THE NAME LISTS ARE COUNTED — the trailing parameter of each 353,
	// after the " :". Two other places legitimately carry a nickname and would
	// be counted as members otherwise: the echo of the joiner's own JOIN, and
	// the <target> field that every numeric addresses to the recipient. The
	// recipient here is one of the thirty, so its own nick appears in the head
	// of BOTH 353 lines as well as in the list.
	std::string				namesOnly;
	std::string::size_type	from = 0;
	std::string::size_type	eol;

	while ((eol = out.find("\r\n", from)) != std::string::npos)
	{
		const std::string		line = out.substr(from, eol - from);
		std::string::size_type	trailing = line.find(" :");

		if (line.find(" 353 ") != std::string::npos
			&& trailing != std::string::npos)
			namesOnly += line.substr(trailing + 2) + " ";
		from = eol + 2;
	}

	// Nothing was lost and nothing was duplicated: every member appears
	// exactly once across all the 353 lines the last joiner received.
	int	seen = 0;

	for (i = 0; i < 30; ++i)
	{
		if (countOf(namesOnly,
				"verylongnickname_" + utils::toString(i + 10)) == 1)
			++seen;
	}
	check(seen == 30,
		"every one of the 30 members appears exactly once across the lines");

	// And no line went over the limit, which is what would have caused the
	// loss: sendToClient truncates at 510 without asking.
	std::string::size_type	start = 0;
	std::string::size_type	pos;
	bool					allFit = true;

	while ((pos = out.find("\r\n", start)) != std::string::npos)
	{
		if (pos - start > irc::MAX_PAYLOAD_LEN)
			allFit = false;
		start = pos + 2;
	}
	check(allFit, "and no single line exceeded 510 bytes before its CRLF");
}

// --- step 2: several channels at once, and the mode gates -----------------

static void	testJoinSeveralChannels(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");

	makeUser(alice, "alice");
	feed(server, alice, "JOIN #a,#b");

	Channel	*a = server.findChannel("#a");
	Channel	*b = server.findChannel("#b");

	check(a != NULL && b != NULL, "both channels are created");
	// GUARDED, because a failing check must fail rather than crash: until the
	// list is split, findChannel returns NULL here and a bare -> would take
	// the whole test binary down with it.
	check(a != NULL && b != NULL && a->isMember(&alice) && b->isMember(&alice),
		"and the sender is in both");
	check(countOf(alice.getOutputBuffer(), " 366 ") == 2,
		"each channel gets its own complete entry sequence");

	// One bad name does not stop the rest of the list. The client asked for
	// two things; failing both because one was wrong would be worse service
	// than the error itself.
	Client	bob(-1, "localhost");

	makeUser(bob, "bob");
	feed(server, bob, "JOIN &nope,#good");
	check(contains(bob.getOutputBuffer(), " 403 bob &nope "),
		"the invalid name in the list is refused");
	check(server.findChannel("#good") != NULL
		&& server.findChannel("#good")->isMember(&bob),
		"and the valid one is still joined");

	// An EMPTY FIELD is skipped in silence, for the same reason a lone empty
	// parameter is (D18): it names no channel, and split keeps it only so that
	// the key list stays aligned.
	Client	carol(-1, "localhost");

	makeUser(carol, "carol");
	feed(server, carol, "JOIN #x,,#y");
	check(countOf(carol.getOutputBuffer(), " 366 ") == 2,
		"an empty field between two names joins the two");
	check(countOf(carol.getOutputBuffer(), " 403 ") == 0,
		"and says nothing about the empty one");
}

// KEYS ARE POSITIONAL, and this is the reason utils::split preserves empty
// fields (see the comment on it in src/Utils.cpp). "JOIN #a,#b ,key2" gives
// #a no key and #b the key; collapse that empty field and key2 lands on #a —
// the user is refused entry to one channel and hands its key to another.
static void	testJoinKeysArePositional(void)
{
	Server	server(6667, "secret");
	Client	setup(-1, "localhost");

	makeUser(setup, "setup");

	Channel	*first = server.getOrCreateChannel("#first");
	Channel	*second = server.getOrCreateChannel("#second");

	first->addMember(&setup);
	second->addMember(&setup);
	first->setKey("chave1");
	second->setKey("chave2");

	Client	alice(-1, "localhost");

	makeUser(alice, "alice");
	feed(server, alice, "JOIN #first,#second chave1,chave2");
	check(first->isMember(&alice) && second->isMember(&alice),
		"matching keys in order open both channels");

	// The same two channels, keys swapped: both must be refused.
	Client	bob(-1, "localhost");

	makeUser(bob, "bob");
	feed(server, bob, "JOIN #first,#second chave2,chave1");
	check(countOf(bob.getOutputBuffer(), " 475 ") == 2,
		"swapped keys are refused on both channels");
	check(!first->isMember(&bob) && !second->isMember(&bob),
		"and neither was joined");

	// The empty-field case itself: no key for the first, a key for the second.
	Channel	*open = server.getOrCreateChannel("#open");

	open->addMember(&setup);

	Client	carol(-1, "localhost");

	makeUser(carol, "carol");
	feed(server, carol, "JOIN #open,#second ,chave2");
	check(open->isMember(&carol),
		"the keyless channel is joined with an empty key field");
	check(second->isMember(&carol),
		"and the key still lands on the SECOND channel, not the first");
}

static void	testJoinKeyGate(void)
{
	Server	server(6667, "secret");
	Client	owner(-1, "localhost");

	makeUser(owner, "owner");
	feed(server, owner, "JOIN #locked");

	Channel	*channel = server.findChannel("#locked");

	channel->setKey("segredo");

	Client	nokey(-1, "localhost");

	makeUser(nokey, "nokey");
	feed(server, nokey, "JOIN #locked");
	check(contains(nokey.getOutputBuffer(),
			" 475 nokey #locked :Cannot join channel (+k)"),
		"no key at all is 475");
	check(!channel->isMember(&nokey), "and does not get in");

	Client	wrong(-1, "localhost");

	makeUser(wrong, "wrong");
	feed(server, wrong, "JOIN #locked outra");
	check(contains(wrong.getOutputBuffer(), " 475 "), "a wrong key is 475");

	Client	right(-1, "localhost");

	makeUser(right, "right");
	feed(server, right, "JOIN #locked segredo");
	check(channel->isMember(&right), "the right key gets in");
	check(!channel->isOperator(&right),
		"and joining with a key does not make anyone an operator");
}

static void	testJoinInviteGate(void)
{
	Server	server(6667, "secret");
	Client	owner(-1, "localhost");

	makeUser(owner, "owner");
	feed(server, owner, "JOIN #private");

	Channel	*channel = server.findChannel("#private");

	channel->setInviteOnly(true);

	Client	stranger(-1, "localhost");

	makeUser(stranger, "stranger");
	feed(server, stranger, "JOIN #private");
	check(contains(stranger.getOutputBuffer(),
			" 473 stranger #private :Cannot join channel (+i)"),
		"an uninvited client is 473");
	check(!channel->isMember(&stranger), "and stays out");

	Client	guest(-1, "localhost");

	makeUser(guest, "guest");
	channel->addInvite(&guest);
	feed(server, guest, "JOIN #private");
	check(channel->isMember(&guest), "an invited client gets in");

	// THE INVITE IS CONSUMED. It is a single-use pass: leaving and walking
	// back in without a fresh invite must fail, or +i means nothing after the
	// first visit.
	check(!channel->isInvited(&guest),
		"and the invitation is spent on the way in");
}

static void	testJoinLimitGate(void)
{
	Server	server(6667, "secret");
	Client	owner(-1, "localhost");
	Client	second(-1, "localhost");

	makeUser(owner, "owner");
	makeUser(second, "second");
	feed(server, owner, "JOIN #small");
	feed(server, second, "JOIN #small");

	Channel	*channel = server.findChannel("#small");

	channel->setUserLimit(2);

	Client	late(-1, "localhost");

	makeUser(late, "late");
	feed(server, late, "JOIN #small");
	check(contains(late.getOutputBuffer(),
			" 471 late #small :Cannot join channel (+l)"),
		"a full channel is 471");
	check(!channel->isMember(&late), "and the latecomer stays out");

	// The limit is a ceiling on members, not on attempts: raise it and the
	// same client walks in.
	channel->setUserLimit(3);
	feed(server, late, "JOIN #small");
	check(channel->isMember(&late), "raising the limit lets them in");
}

// THE ORDER OF THE GATES IS A DECISION, not an accident, so it is locked here.
// A channel can be +i and +k at once, and only one numeric comes back.
static void	testGateOrder(void)
{
	Server	server(6667, "secret");
	Client	owner(-1, "localhost");

	makeUser(owner, "owner");
	feed(server, owner, "JOIN #fort");

	Channel	*channel = server.findChannel("#fort");

	channel->setInviteOnly(true);
	channel->setKey("segredo");
	channel->setUserLimit(1);

	Client	nobody(-1, "localhost");

	makeUser(nobody, "nobody");
	feed(server, nobody, "JOIN #fort");
	check(contains(nobody.getOutputBuffer(), " 473 "),
		"invite-only is reported first: 473 before 475 and 471");
	check(countOf(nobody.getOutputBuffer(), " 475 ") == 0
		&& countOf(nobody.getOutputBuffer(), " 471 ") == 0,
		"and only one numeric comes back, not three");

	// AN INVITE OPENS +i AND NOTHING ELSE. It is not a master key: the channel
	// key still has to be produced.
	Client	guest(-1, "localhost");

	makeUser(guest, "guest");
	channel->addInvite(&guest);
	feed(server, guest, "JOIN #fort");
	check(contains(guest.getOutputBuffer(), " 475 "),
		"an invited client without the key is still 475");
	check(channel->isInvited(&guest),
		"and the invitation is NOT spent by a failed attempt");

	// With the key as well, only the limit is left — and it is full.
	feed(server, guest, "JOIN #fort segredo");
	check(contains(guest.getOutputBuffer(), " 471 "),
		"key accepted, limit still refuses: 471 is the last gate");

	channel->setUserLimit(5);
	feed(server, guest, "JOIN #fort segredo");
	check(channel->isMember(&guest), "with all three satisfied, they get in");
}

// --- step 3: leaving ------------------------------------------------------

static void	testPartErrors(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");

	makeUser(alice, "alice");

	feed(server, alice, "PART");
	checkEqual(alice.getOutputBuffer(),
		":ircserv 461 alice PART :Not enough parameters\r\n",
		"PART with no parameter is 461");

	Client	bob(-1, "localhost");

	makeUser(bob, "bob");
	feed(server, bob, "PART #nope");
	checkEqual(bob.getOutputBuffer(),
		":ircserv 403 bob #nope :No such channel\r\n",
		"PART on a channel that does not exist is 403");

	// THE CHANNEL EXISTS BUT THE SENDER IS NOT IN IT. A different error from
	// 403, and the difference matters: 403 says the room is not there, 442
	// says it is and you are not.
	Client	owner(-1, "localhost");

	makeUser(owner, "owner");
	feed(server, owner, "JOIN #room");

	Client	outsider(-1, "localhost");

	makeUser(outsider, "outsider");
	feed(server, outsider, "PART #room");
	checkEqual(outsider.getOutputBuffer(),
		":ircserv 442 outsider #room :You're not on that channel\r\n",
		"PART from a non-member is 442");
	check(server.findChannel("#room")->memberCount() == 1,
		"and takes nobody out of the channel");
}

static void	testPartBroadcast(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");
	Client	bob(-1, "localhost");

	makeUser(alice, "alice");
	makeUser(bob, "bob");
	feed(server, alice, "JOIN #room");
	feed(server, bob, "JOIN #room");

	Client	watcher(-1, "localhost");

	makeUser(watcher, "watcher");
	feed(server, watcher, "JOIN #room");

	feed(server, bob, "PART #room :ate mais");

	// THE LEAVER GETS THE ECHO TOO. That is what tells their client to close
	// the window; without it irssi leaves a dead channel window open.
	check(contains(bob.getOutputBuffer(),
			":bob!bob@localhost PART #room :ate mais\r\n"),
		"the client leaving receives its own PART");
	check(contains(alice.getOutputBuffer(),
			":bob!bob@localhost PART #room :ate mais\r\n"),
		"and so does everyone still in the channel");
	check(!server.findChannel("#room")->isMember(&bob),
		"and the member is actually removed");

	// NO REASON MEANS NO TRAILING PARAMETER, rather than an invented one. The
	// client shows "has left" either way, and making up a reason would put
	// words in the user's mouth.
	feed(server, watcher, "PART #room");
	check(contains(alice.getOutputBuffer(),
			":watcher!watcher@localhost PART #room\r\n"),
		"a PART with no reason carries no trailing parameter");
}

// THE CHANNEL DIES WITH ITS LAST MEMBER, and everything it held dies with it:
// topic, modes, key, and the operator list. That is what makes walking back in
// a fresh start rather than a resurrection.
static void	testChannelDisappearsWhenEmpty(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");

	makeUser(alice, "alice");
	feed(server, alice, "JOIN #temp");

	Channel	*channel = server.findChannel("#temp");

	channel->setTopic("assunto qualquer");
	channel->setKey("segredo");

	feed(server, alice, "PART #temp");
	check(server.findChannel("#temp") == NULL,
		"the last member leaving destroys the channel");

	// Walking back in creates a NEW channel: no topic, no key, and the person
	// who opened it is its operator again.
	Client	bob(-1, "localhost");

	makeUser(bob, "bob");
	feed(server, bob, "JOIN #temp");

	Channel	*reborn = server.findChannel("#temp");

	check(reborn != NULL && reborn->isOperator(&bob),
		"whoever re-creates it is its operator");
	check(reborn != NULL && reborn->getTopic().empty(),
		"the old topic did not survive");
	check(reborn != NULL && !reborn->hasKey(),
		"and neither did the old key");
}

// A CHANNEL CAN END UP WITH NO OPERATOR AT ALL, and that is deliberate. The
// alternative — promoting whoever happens to be next — picks a winner out of a
// std::set ordered by memory address, which is to say at random. Real servers
// leave the channel opless.
static void	testChannelSurvivesLosingItsOperator(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");
	Client	bob(-1, "localhost");

	makeUser(alice, "alice");
	makeUser(bob, "bob");
	feed(server, alice, "JOIN #room");
	feed(server, bob, "JOIN #room");

	feed(server, alice, "PART #room");

	Channel	*channel = server.findChannel("#room");

	check(channel != NULL, "the channel survives its operator leaving");
	check(channel != NULL && channel->isMember(&bob),
		"the remaining member is still in it");
	check(channel != NULL && !channel->isOperator(&bob),
		"and is NOT promoted to fill the vacancy");
}

static void	testPartSeveralChannels(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");
	Client	holder(-1, "localhost");

	makeUser(alice, "alice");
	makeUser(holder, "holder");

	// A second member keeps both channels alive after alice leaves, so the
	// assertions below are about membership rather than destruction.
	feed(server, holder, "JOIN #a,#b");
	feed(server, alice, "JOIN #a,#b");
	feed(server, alice, "PART #a,#b :saindo");

	check(!server.findChannel("#a")->isMember(&alice)
		&& !server.findChannel("#b")->isMember(&alice),
		"PART takes a list, like JOIN");
	check(countOf(alice.getOutputBuffer(), " PART ") == 2,
		"with one echo per channel");

	// One bad name does not cancel the rest, same rule as JOIN.
	feed(server, holder, "PART #nope,#a");
	check(contains(holder.getOutputBuffer(), " 403 holder #nope "),
		"the missing channel is refused");
	check(server.findChannel("#a") == NULL,
		"and the valid one was still left — which emptied it");
}

// --- step 4: talking ------------------------------------------------------

static void	testPrivmsgErrors(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");

	makeUser(alice, "alice");

	feed(server, alice, "PRIVMSG");
	checkEqual(alice.getOutputBuffer(),
		":ircserv 411 alice :No recipient given (PRIVMSG)\r\n",
		"PRIVMSG with no target is 411, and names the command");

	Client	bob(-1, "localhost");

	makeUser(bob, "bob");
	feed(server, bob, "PRIVMSG #room");
	checkEqual(bob.getOutputBuffer(),
		":ircserv 412 bob :No text to send\r\n",
		"a target with no text is 412");

	// ":" ON ITS OWN IS A PRESENT BUT EMPTY TRAILING PARAMETER, which is why
	// the parser keeps hasTrailing. It is still nothing to say, so it is still
	// 412 — the alternative is relaying an empty line to the whole channel.
	Client	carol(-1, "localhost");

	makeUser(carol, "carol");

	Message	emptyText = parseMessage("PRIVMSG #room :");

	check(emptyText.params.size() == 2 && emptyText.hasTrailing,
		"the parser does keep an empty trailing parameter");
	cmdPrivmsg(server, carol, emptyText);
	check(contains(carol.getOutputBuffer(), " 412 carol :No text to send"),
		"and an empty text is still 412");

	Client	dave(-1, "localhost");

	makeUser(dave, "dave");
	feed(server, dave, "PRIVMSG #nope :oi");
	checkEqual(dave.getOutputBuffer(),
		":ircserv 403 dave #nope :No such channel\r\n",
		"a channel that does not exist is 403");
}

// D13: SPEAKING IN A CHANNEL YOU ARE NOT IN IS REFUSED. We implement no +n,
// but letting a stranger talk into a room they never entered is worse than
// refusing, and 404 is already in the table.
static void	testPrivmsgToChannel(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");
	Client	bob(-1, "localhost");
	Client	carol(-1, "localhost");

	makeUser(alice, "alice");
	makeUser(bob, "bob");
	makeUser(carol, "carol");
	feed(server, alice, "JOIN #room");
	feed(server, bob, "JOIN #room");
	feed(server, carol, "JOIN #room");

	Client	outsider(-1, "localhost");

	makeUser(outsider, "outsider");
	feed(server, outsider, "PRIVMSG #room :deixem eu falar");
	check(contains(outsider.getOutputBuffer(),
			" 404 outsider #room :Cannot send to channel"),
		"a non-member talking to the channel is 404 (D13)");
	check(!contains(alice.getOutputBuffer(), "deixem eu falar"),
		"and the message reaches nobody");

	const std::string	beforeAlice = alice.getOutputBuffer();

	feed(server, alice, "PRIVMSG #room :ola pessoal");

	// THE SENDER DOES NOT GET THEIR OWN MESSAGE BACK. That asymmetry with JOIN
	// is the protocol's, not ours: the client already displayed what the user
	// typed, and echoing it would double every line on screen.
	checkEqual(alice.getOutputBuffer(), beforeAlice,
		"the sender receives nothing back");
	check(contains(bob.getOutputBuffer(),
			":alice!alice@localhost PRIVMSG #room :ola pessoal\r\n"),
		"every other member receives it, with the sender's prefix");
	check(contains(carol.getOutputBuffer(),
			":alice!alice@localhost PRIVMSG #room :ola pessoal\r\n"),
		"including the third one");

	// The text is a trailing parameter, so spaces and further colons inside it
	// are content. This is the parser's rule showing up where it matters.
	feed(server, alice, "PRIVMSG #room :12:30 e hora do almoco, ok?");
	check(contains(bob.getOutputBuffer(),
			":12:30 e hora do almoco, ok?\r\n"),
		"colons and spaces inside the text survive intact");

	// The channel is found case-insensitively, and the relayed line carries
	// the channel's original spelling (D5).
	feed(server, alice, "PRIVMSG #ROOM :de novo");
	check(contains(bob.getOutputBuffer(), " PRIVMSG #room :de novo"),
		"the target is matched ignoring case and relayed in its own spelling");
}

// The nick path. In a unit test findClientByNick always returns NULL, because
// clients only enter Server::_clients through accept() — so what is provable
// here is the ERROR, and the delivery is proved over a socket in
// tests/it/privmsg.sh. See section 1.1 of docs/FASE3.md.
static void	testPrivmsgToUnknownUser(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");

	makeUser(alice, "alice");
	feed(server, alice, "PRIVMSG ninguem :ola");
	checkEqual(alice.getOutputBuffer(),
		":ircserv 401 alice ninguem :No such nick/channel\r\n",
		"a message to a nickname nobody holds is 401");
}

// --- step 6: the topic ----------------------------------------------------

static void	testTopicErrors(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");

	makeUser(alice, "alice");

	feed(server, alice, "TOPIC");
	checkEqual(alice.getOutputBuffer(),
		":ircserv 461 alice TOPIC :Not enough parameters\r\n",
		"TOPIC with no parameter is 461");

	Client	bob(-1, "localhost");

	makeUser(bob, "bob");
	feed(server, bob, "TOPIC #nope");
	checkEqual(bob.getOutputBuffer(),
		":ircserv 403 bob #nope :No such channel\r\n",
		"TOPIC on a channel that does not exist is 403");

	Client	owner(-1, "localhost");

	makeUser(owner, "owner");
	feed(server, owner, "JOIN #room");

	Client	outsider(-1, "localhost");

	makeUser(outsider, "outsider");
	feed(server, outsider, "TOPIC #room");
	checkEqual(outsider.getOutputBuffer(),
		":ircserv 442 outsider #room :You're not on that channel\r\n",
		"a non-member cannot even read the topic here");
}

// READING IS NOT WRITING, and TOPIC is the one command in this file that does
// both depending on its parameters. Querying requires nothing beyond
// membership — not operator status, and not +t.
static void	testTopicQuery(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");
	Client	bob(-1, "localhost");

	makeUser(alice, "alice");
	makeUser(bob, "bob");
	feed(server, alice, "JOIN #room");
	feed(server, bob, "JOIN #room");

	Client	reader(-1, "localhost");

	makeUser(reader, "reader");
	feed(server, reader, "JOIN #room");
	feed(server, reader, "TOPIC #room");
	check(contains(reader.getOutputBuffer(),
			":ircserv 331 reader #room :No topic is set\r\n"),
		"a channel with no topic answers 331");

	// +t restricts SETTING, never reading. A plain member under +t still gets
	// an answer.
	server.findChannel("#room")->setTopicRestricted(true);
	server.findChannel("#room")->setTopic("assunto do dia");
	feed(server, reader, "TOPIC #room");
	check(contains(reader.getOutputBuffer(),
			":ircserv 332 reader #room :assunto do dia\r\n"),
		"a channel with a topic answers 332, even under +t to a plain member");
}

static void	testTopicChange(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");
	Client	bob(-1, "localhost");

	makeUser(alice, "alice");
	makeUser(bob, "bob");
	feed(server, alice, "JOIN #room");
	feed(server, bob, "JOIN #room");

	Channel	*channel = server.findChannel("#room");

	// WITHOUT +t ANY MEMBER MAY SET IT. That is the default, and it is why +t
	// exists as a mode at all.
	feed(server, bob, "TOPIC #room :bob mandou aqui");
	checkEqual(channel->getTopic(), "bob mandou aqui",
		"any member can set the topic when +t is off");
	check(contains(alice.getOutputBuffer(),
			":bob!bob@localhost TOPIC #room :bob mandou aqui\r\n"),
		"and the change is announced to the channel");
	check(contains(bob.getOutputBuffer(),
			":bob!bob@localhost TOPIC #room :bob mandou aqui\r\n"),
		"including back to whoever set it, as confirmation");

	// Under +t, the same client is refused and the topic does not move.
	channel->setTopicRestricted(true);
	feed(server, bob, "TOPIC #room :tentando de novo");
	check(contains(bob.getOutputBuffer(),
			" 482 bob #room :You're not channel operator"),
		"a plain member under +t is 482");
	checkEqual(channel->getTopic(), "bob mandou aqui",
		"and the topic is unchanged");

	// The operator still can.
	feed(server, alice, "TOPIC #room :a chefe decide");
	checkEqual(channel->getTopic(), "a chefe decide",
		"an operator sets it even under +t");

	// A topic without a colon is still a topic — one word, no trailing marker.
	feed(server, alice, "TOPIC #room umapalavra");
	checkEqual(channel->getTopic(), "umapalavra",
		"a single-word topic needs no trailing marker");
}

// CLEARING IS AN EMPTY TRAILING PARAMETER, and this is where the parser's
// hasTrailing earns its keep: "TOPIC #c" and "TOPIC #c :" are different lines
// on the wire, and they must do different things — ask, and erase. An
// implementation that dropped the empty trailing would make them identical and
// there would be no way to clear a topic at all.
static void	testTopicClear(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");

	makeUser(alice, "alice");
	feed(server, alice, "JOIN #room");

	Channel	*channel = server.findChannel("#room");

	channel->setTopic("algo escrito");

	Message	clear = parseMessage("TOPIC #room :");

	check(clear.params.size() == 2 && clear.params[1].empty()
		&& clear.hasTrailing,
		"the parser keeps the empty trailing parameter that clearing needs");
	cmdTopic(server, alice, clear);
	check(channel->getTopic().empty(), "an empty topic clears it");
	check(contains(alice.getOutputBuffer(),
			":alice!alice@localhost TOPIC #room :\r\n"),
		"and the clearing is announced, with an empty trailing parameter");

	feed(server, alice, "TOPIC #room");
	check(contains(alice.getOutputBuffer(), " 331 alice #room "),
		"after clearing, the query answers 331 again");
}

// --- step 7: KICK ---------------------------------------------------------

static void	testKickErrors(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");
	Client	bob(-1, "localhost");

	makeUser(alice, "alice");
	makeUser(bob, "bob");
	feed(server, alice, "JOIN #room");
	feed(server, bob, "JOIN #room");

	feed(server, alice, "KICK #room");
	check(contains(alice.getOutputBuffer(),
			" 461 alice KICK :Not enough parameters"),
		"KICK with only a channel is 461");

	Client	stranger(-1, "localhost");

	makeUser(stranger, "stranger");
	feed(server, stranger, "KICK #nope bob");
	check(contains(stranger.getOutputBuffer(), " 403 stranger #nope "),
		"KICK on a channel that does not exist is 403");

	feed(server, stranger, "KICK #room bob");
	check(contains(stranger.getOutputBuffer(),
			" 442 stranger #room :You're not on that channel"),
		"a non-member cannot kick, and hears 442 before anything else");
	check(server.findChannel("#room")->isMember(&bob),
		"and bob is still there");

	// A MEMBER IS NOT AN OPERATOR. This is the line that makes the channel
	// operator mean something.
	feed(server, bob, "KICK #room alice");
	check(contains(bob.getOutputBuffer(),
			" 482 bob #room :You're not channel operator"),
		"a plain member kicking is 482");
	check(server.findChannel("#room")->isMember(&alice),
		"and the operator is still there");

	// 441, NOT 401 — decision D10. RFC 2812 lists ERR_USERNOTINCHANNEL for
	// KICK and does not list ERR_NOSUCHNICK, because the question KICK asks is
	// "is this person in THIS channel", not "does this person exist".
	feed(server, alice, "KICK #room ninguem");
	check(contains(alice.getOutputBuffer(),
			" 441 alice ninguem #room :They aren't on that channel"),
		"kicking somebody who is not in the channel is 441");
}

static void	testKickRemovesTheVictim(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");
	Client	bob(-1, "localhost");
	Client	carol(-1, "localhost");

	makeUser(alice, "alice");
	makeUser(bob, "bob");
	makeUser(carol, "carol");
	feed(server, alice, "JOIN #room");
	feed(server, bob, "JOIN #room");
	feed(server, carol, "JOIN #room");

	feed(server, alice, "KICK #room bob :comporte-se");

	// THE VICTIM IS TOLD, and told BEFORE being removed — that echo is what
	// closes the channel window in their client. Remove first and they are
	// left staring at a channel they are no longer in.
	check(contains(bob.getOutputBuffer(),
			":alice!alice@localhost KICK #room bob :comporte-se\r\n"),
		"the victim receives the KICK");
	check(contains(carol.getOutputBuffer(),
			":alice!alice@localhost KICK #room bob :comporte-se\r\n"),
		"and so does everyone else in the channel");
	check(!server.findChannel("#room")->isMember(&bob),
		"the victim is out");
	check(server.findChannel("#room")->isMember(&carol),
		"and nobody else moved");

	// The Client object is untouched — KICK removes a membership, not a
	// connection.
	checkEqual(bob.getNickname(), "bob",
		"the victim is still a connected client, just not a member");
	check(!bob.isDisconnecting(), "and is certainly not disconnected");
}

static void	testKickDetails(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");
	Client	bob(-1, "localhost");

	makeUser(alice, "alice");
	makeUser(bob, "bob");
	feed(server, alice, "JOIN #room");
	feed(server, bob, "JOIN #room");

	// NO REASON MEANS THE KICKER'S NICK, which is the convention every
	// deployed server follows: the line reads "bob was kicked by alice
	// (alice)".
	feed(server, alice, "KICK #room bob");
	check(contains(bob.getOutputBuffer(),
			":alice!alice@localhost KICK #room bob :alice\r\n"),
		"a KICK with no reason uses the kicker's nickname as the reason");

	// The victim's nick is matched ignoring case (RFC 2812 section 2.2), and
	// the line carries the victim's OWN spelling.
	Client	nick42(-1, "localhost");

	makeUser(nick42, "Nick[42]");
	feed(server, nick42, "JOIN #room");
	feed(server, alice, "KICK #room nick{42} :casemapping");
	check(contains(nick42.getOutputBuffer(), " KICK #room Nick[42] :casemapping"),
		"the victim is found under IRC casemapping, and named as they spell it");

	// AN OPERATOR MAY KICK AN OPERATOR, including themselves. Nothing in the
	// protocol ranks two operators, and refusing would need a rule we do not
	// have.
	Client	dave(-1, "localhost");

	makeUser(dave, "dave");
	feed(server, dave, "JOIN #other");
	feed(server, dave, "KICK #other dave :saindo por conta propria");
	check(server.findChannel("#other") == NULL,
		"an operator can kick themselves, and the emptied channel is removed");
}

// --- step 8: INVITE -------------------------------------------------------

static void	testInviteErrors(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");

	makeUser(alice, "alice");
	feed(server, alice, "JOIN #room");

	feed(server, alice, "INVITE bob");
	check(contains(alice.getOutputBuffer(),
			" 461 alice INVITE :Not enough parameters"),
		"INVITE with only a nickname is 461");

	// 442, NOT 403, FOR A CHANNEL THAT DOES NOT EXIST — decision D21. RFC 2812
	// section 3.2.7 lists 461, 401, 442, 443 and 482 for INVITE and does NOT
	// list ERR_NOSUCHCHANNEL. "You're not on that channel" is true of a channel
	// that is not there, and inventing a code the RFC left out of this command
	// is exactly what ARCHITECTURE.md section 10 warns against.
	feed(server, alice, "INVITE bob #nope");
	check(contains(alice.getOutputBuffer(),
			" 442 alice #nope :You're not on that channel"),
		"inviting to a channel that does not exist is 442 (D21)");

	Client	outsider(-1, "localhost");

	makeUser(outsider, "outsider");
	feed(server, outsider, "INVITE bob #room");
	check(contains(outsider.getOutputBuffer(),
			" 442 outsider #room :You're not on that channel"),
		"and so is inviting to a channel you are not in");

	// In a unit test findClientByNick always returns NULL, so this is the one
	// INVITE path provable here; the delivery is proved over a socket in
	// tests/it/invite.sh. See section 1.1 of docs/FASE3.md.
	feed(server, alice, "INVITE ninguem #room");
	check(contains(alice.getOutputBuffer(),
			" 401 alice ninguem :No such nick/channel"),
		"inviting somebody who is not connected is 401");
}

// WITHOUT +i ANY MEMBER MAY INVITE; with it, only operators. That is the whole
// point of the mode: it turns the guest list into an operator's decision.
static void	testInvitePrivileges(void)
{
	Server	server(6667, "secret");
	Client	alice(-1, "localhost");
	Client	bob(-1, "localhost");

	makeUser(alice, "alice");
	makeUser(bob, "bob");
	feed(server, alice, "JOIN #room");
	feed(server, bob, "JOIN #room");

	Channel	*channel = server.findChannel("#room");

	// bob is a plain member. With the channel open, his 401 proves he got past
	// the privilege check — the failure is the missing target, not his rank.
	feed(server, bob, "INVITE ninguem #room");
	check(contains(bob.getOutputBuffer(), " 401 bob ninguem "),
		"a plain member may invite while the channel is open");

	channel->setInviteOnly(true);
	feed(server, bob, "INVITE ninguem #room");
	check(contains(bob.getOutputBuffer(),
			" 482 bob #room :You're not channel operator"),
		"under +i, a plain member inviting is 482");

	// The operator still can, and reaches the target lookup.
	feed(server, alice, "INVITE ninguem #room");
	check(contains(alice.getOutputBuffer(), " 401 alice ninguem "),
		"the operator gets past the privilege check under +i");
}

void	runCommandChannelTests(void)
{
	testModeErrors();
	testUserModeIsIgnoredSilently();
	testModeQuery();
	testModePrivilegeAndUnknownFlag();
	testModeInviteOnlyAndTopicLock();
	testModeThenJoinGate();
	testModeKey();
	testModeLimit();
	testModeOperator();
	testModeParameterConsumption();
	testJoinErrors();
	testJoinWithEmptyChannelListIsSilent();
	testJoinCreatesChannel();
	testJoinExistingChannel();
	testJoinIsIdempotentAndCaseInsensitive();
	testNamesReplySplitsOverManyLines();
	testJoinSeveralChannels();
	testJoinKeysArePositional();
	testJoinKeyGate();
	testJoinInviteGate();
	testJoinLimitGate();
	testGateOrder();
	testPartErrors();
	testPartBroadcast();
	testChannelDisappearsWhenEmpty();
	testChannelSurvivesLosingItsOperator();
	testPartSeveralChannels();
	testPrivmsgErrors();
	testPrivmsgToChannel();
	testPrivmsgToUnknownUser();
	testTopicErrors();
	testTopicQuery();
	testTopicChange();
	testTopicClear();
	testKickErrors();
	testKickRemovesTheVictim();
	testKickDetails();
	testInviteErrors();
	testInvitePrivileges();
}
