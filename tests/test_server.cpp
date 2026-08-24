#include <string>

#include "Channel.hpp"
#include "Client.hpp"
#include "Command.hpp"
#include "Limits.hpp"
#include "Server.hpp"

#include "harness.hpp"

// Unit tests for the transport track's write path.
//
// NO SOCKET IS OPENED HERE, and that is not a limitation of the tests — it is
// the design paying off. Server's constructor deliberately touches nothing:
// the listening socket is created by run(), which these tests never call. And
// sendToClient does not write to an fd either; it truncates, appends CRLF and
// hands the result to Client::queueOutput. So the whole outbound contract —
// the 510-byte limit, the CRLF, the SendQ ceiling, the deferred disconnect —
// is testable with two objects on the stack.
//
// The fd of -1 is a deliberate booby trap: if any of this ever starts calling
// send() behind the seam, the test suite will notice instead of the evaluator.

static void	testOutgoingTruncation(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	server.sendToClient(client, "PING :token");
	checkEqual(client.getOutputBuffer(), "PING :token\r\n",
		"sendToClient appends CRLF and nothing else");

	Client	longLine(-1, "localhost");

	server.sendToClient(longLine, std::string(600, 'a'));
	check(longLine.getOutputBuffer().size() == irc::MAX_PAYLOAD_LEN + 2,
		"a 600-byte line goes out as 510 + CRLF");
	checkEqual(longLine.getOutputBuffer().substr(irc::MAX_PAYLOAD_LEN),
		"\r\n",
		"the CRLF survives the truncation, and is not cut in half");

	// The boundary in both directions: 510 is legal and untouched, 511 is not.
	Client	exact(-1, "localhost");

	server.sendToClient(exact, std::string(irc::MAX_PAYLOAD_LEN, 'b'));
	check(exact.getOutputBuffer().size() == irc::MAX_PAYLOAD_LEN + 2,
		"exactly 510 bytes is not truncated");

	Client	overByOne(-1, "localhost");

	server.sendToClient(overByOne, std::string(irc::MAX_PAYLOAD_LEN + 1, 'c'));
	check(overByOne.getOutputBuffer().size() == irc::MAX_PAYLOAD_LEN + 2,
		"511 bytes loses exactly one");
}

static void	testSendQueueCeiling(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	// 200 * 512 = 102400, comfortably past MAX_OUTPUT_QUEUE (65536). This is a
	// client that stopped reading — Ctrl+Z in nc, or hostile — while traffic
	// kept arriving for it.
	for (int i = 0; i < 200; ++i)
		server.sendToClient(client, std::string(irc::MAX_PAYLOAD_LEN, 'x'));

	check(client.isDisconnecting(),
		"overflowing SendQ marks the client");
	checkEqual(client.getQuitReason(), "SendQ exceeded",
		"and records why, for the ERROR line and the log");
	check(client.getOutputBuffer().size() <= irc::MAX_OUTPUT_QUEUE,
		"the queue never grew past its ceiling");
}

static void	testDeferredDisconnect(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	server.disconnectClient(client, "Request too long");
	check(client.isDisconnecting(), "disconnectClient marks the client");
	checkEqual(client.getOutputBuffer(), "ERROR :Request too long\r\n",
		"and queues the ERROR the reap will flush");

	// The client is STILL A VALID OBJECT here. Nothing was deleted: that only
	// happens in reapDisconnected, at the end of a poll iteration, which is
	// what makes "QUIT :bye\r\nPRIVMSG #c :x\r\n" in one packet safe.
	checkEqual(client.getHostname(), "localhost",
		"the Client& stays valid after disconnectClient returns");

	// Second call is a no-op. Without that guard, a disconnect on a client
	// whose queue is already full recurses through sendToClient until the
	// stack runs out.
	server.disconnectClient(client, "some other reason");
	checkEqual(client.getQuitReason(), "Request too long",
		"a second disconnect does not overwrite the first reason");
	checkEqual(client.getOutputBuffer(), "ERROR :Request too long\r\n",
		"and does not queue a second ERROR");
}

static void	testDisconnectOnFullQueueTerminates(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	// The recursion trap itself, end to end: fill the queue so that the ERROR
	// cannot be queued, then disconnect. Reaching the next line at all is the
	// assertion — infinite recursion would take the test binary down with it.
	for (int i = 0; i < 200; ++i)
		server.sendToClient(client, std::string(irc::MAX_PAYLOAD_LEN, 'x'));
	server.disconnectClient(client, "another reason");

	checkEqual(client.getQuitReason(), "SendQ exceeded",
		"disconnecting a client with a full queue terminates");
}

// How many times `needle` appears in `haystack`. "Exactly once" is the whole
// point of broadcastToPeers, and "at least once" would pass even when the bug
// is present.
static int	countOccurrences(const std::string &haystack,
				const std::string &needle)
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

static void	testChannelLookupIsCaseInsensitive(void)
{
	Server	server(6667, "secret");

	check(server.findChannel("#nope") == NULL,
		"findChannel returns NULL for a channel that does not exist");

	Channel	*created = server.getOrCreateChannel("#Dev");

	check(created != NULL, "getOrCreateChannel creates a channel");
	checkEqual(created->getName(), "#Dev",
		"the channel keeps the ORIGINAL spelling for display");

	// The silent-failure case from decision D5: without the lowercased key,
	// this returns NULL and a second #dev is created alongside the first.
	check(server.findChannel("#dev") == created,
		"#dev finds the channel created as #Dev");
	check(server.findChannel("#DEV") == created,
		"and so does #DEV");
	check(server.getOrCreateChannel("#dEv") == created,
		"getOrCreateChannel does not create a second one under another case");

	server.removeChannel("#DEV");
	check(server.findChannel("#Dev") == NULL,
		"removeChannel is case-insensitive too");
}

static void	testBroadcastToChannel(void)
{
	Client	alice(-1, "localhost");
	Client	bob(-1, "localhost");
	Server	server(6667, "secret");

	Channel	*channel = server.getOrCreateChannel("#room");

	channel->addMember(&alice);
	channel->addMember(&bob);

	server.broadcastToChannel(*channel, "HELLO", NULL);
	checkEqual(alice.getOutputBuffer(), "HELLO\r\n",
		"broadcastToChannel reaches every member");
	checkEqual(bob.getOutputBuffer(), "HELLO\r\n",
		"broadcastToChannel reaches the second member too");

	server.broadcastToChannel(*channel, "AGAIN", &alice);
	checkEqual(alice.getOutputBuffer(), "HELLO\r\n",
		"the `except` client is skipped — how PRIVMSG avoids echoing back");
	checkEqual(bob.getOutputBuffer(), "HELLO\r\nAGAIN\r\n",
		"while everyone else still receives it");
}

static void	testBroadcastToPeersDeduplicates(void)
{
	Client	alice(-1, "localhost");
	Client	bob(-1, "localhost");
	Client	stranger(-1, "localhost");
	Server	server(6667, "secret");

	// Alice and Bob share TWO channels. This is the case that makes the naive
	// implementation — looping broadcastToChannel over the origin's channels —
	// deliver Bob two copies of every QUIT and every NICK.
	Channel	*first = server.getOrCreateChannel("#one");
	Channel	*second = server.getOrCreateChannel("#two");
	Channel	*elsewhere = server.getOrCreateChannel("#elsewhere");

	first->addMember(&alice);
	first->addMember(&bob);
	second->addMember(&alice);
	second->addMember(&bob);
	elsewhere->addMember(&stranger);

	server.broadcastToPeers(alice, "QUIT :bye", false);

	check(countOccurrences(bob.getOutputBuffer(), "QUIT :bye") == 1,
		"a peer in TWO shared channels receives the line exactly once");
	checkEqual(alice.getOutputBuffer(), "",
		"includeOrigin=false leaves the origin out — the QUIT case");
	checkEqual(stranger.getOutputBuffer(), "",
		"someone who shares no channel receives nothing");
}

static void	testBroadcastToPeersIncludingOrigin(void)
{
	Client	alice(-1, "localhost");
	Client	bob(-1, "localhost");
	Server	server(6667, "secret");

	Channel	*channel = server.getOrCreateChannel("#room");

	channel->addMember(&alice);
	channel->addMember(&bob);

	server.broadcastToPeers(alice, "NICK :newnick", true);
	check(countOccurrences(alice.getOutputBuffer(), "NICK :newnick") == 1,
		"includeOrigin=true echoes to the origin — the NICK case");
	check(countOccurrences(bob.getOutputBuffer(), "NICK :newnick") == 1,
		"and still reaches the peers exactly once");

	// A client that has joined nothing at all still has to see its own NICK.
	Client	loner(-1, "localhost");

	server.broadcastToPeers(loner, "NICK :solo", true);
	checkEqual(loner.getOutputBuffer(), "NICK :solo\r\n",
		"a client in no channel still gets its own NICK echoed");
}

static void	testCommandTable(void)
{
	CommandTable	table = buildCommandTable();

	check(table.find("PASS") != table.end(), "table: PASS is registered");
	check(table.find("NICK") != table.end(), "table: NICK is registered");
	check(table.find("USER") != table.end(), "table: USER is registered");
	check(table.find("QUIT") != table.end(), "table: QUIT is registered");
	check(table.find("PING") != table.end(), "table: PING is registered");
	check(table.find("PONG") != table.end(), "table: PONG is registered");
	check(table.find("CAP") != table.end(),
		"table: CAP is registered, and answers since D17");

	// The dispatcher uppercases before looking up, so the keys must be
	// uppercase. A lowercase key would simply never be found — silently.
	check(table.find("pass") == table.end(),
		"table: keys are uppercase, matching what the dispatcher searches for");

	// MODE is the first domain entry to land, ahead of JOIN, because irssi
	// sends "MODE <nick> +i" unprompted right after registering and a 421 in
	// its status window is an error the subject forbids. See step 0.6 of
	// docs/FASE3.md.
	check(table.find("MODE") != table.end(), "table: MODE is registered");

	// Documents the current state rather than a desired one: the remaining
	// domain entries stay commented out in src/CommandTable.cpp until their
	// handlers have bodies, because registering one earlier is an undefined
	// reference that breaks BOTH binaries. Each flips as it lands.
	check(table.find("JOIN") != table.end(), "table: JOIN is registered");
	check(table.find("PART") != table.end(), "table: PART is registered");
	check(table.find("PRIVMSG") == table.end(),
		"table: PRIVMSG is not wired up yet");
	// WHO and WHOIS are registered for the same reason CAP is: irssi sends
	// them by itself, and 421 in its status window is an error the subject
	// forbids. See step 1.5 of docs/FASE3.md.
	check(table.find("WHO") != table.end(), "table: WHO is registered");
	check(table.find("WHOIS") != table.end(), "table: WHOIS is registered");
	check(table.size() == 12,
		"table: seven transport, MODE, JOIN, PART, WHO and WHOIS");
}

void	runServerTests(void)
{
	testCommandTable();
	testOutgoingTruncation();
	testSendQueueCeiling();
	testDeferredDisconnect();
	testDisconnectOnFullQueueTerminates();
	testChannelLookupIsCaseInsensitive();
	testBroadcastToChannel();
	testBroadcastToPeersDeduplicates();
	testBroadcastToPeersIncludingOrigin();
}
