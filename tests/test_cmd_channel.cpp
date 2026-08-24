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

void	runCommandChannelTests(void)
{
	testModeErrors();
	testUserModeIsIgnoredSilently();
	testModeQuery();
	testModeChangeIsNotImplementedYet();
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
}
