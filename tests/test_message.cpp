#include "Message.hpp"

#include "harness.hpp"

// The parser turns one raw line into a Message. Every input here is a
// hand-built string: no state, no socket, nothing to set up.

// The three examples from docs/ARCHITECTURE.md section 5, verbatim. If these
// ever disagree with the doc, the doc wins.
static void	testArchitectureExamples(void)
{
	Message	msg;

	msg = parseMessage("PRIVMSG #chan :hello world");
	checkEqual(msg.command, "PRIVMSG",
		"grammar: PRIVMSG is read as the command");
	check(msg.params.size() == 2,
		"grammar: PRIVMSG example yields two params");
	checkEqual(msg.params[1], "hello world",
		"grammar: the trailing keeps its space and stays ONE param");

	msg = parseMessage("JOIN #a,#b key1,key2");
	checkEqual(msg.command, "JOIN",
		"grammar: JOIN is read as the command");
	check(msg.params.size() == 2,
		"grammar: JOIN example yields two params");
	checkEqual(msg.params[0], "#a,#b",
		"grammar: the channel list stays one param, commas included");

	msg = parseMessage("MODE #chan +o bob");
	checkEqual(msg.command, "MODE",
		"grammar: MODE is read as the command");
	check(msg.params.size() == 3,
		"grammar: MODE example yields three params");
	checkEqual(msg.params[2], "bob",
		"grammar: the last MODE param is the target nick");
}

static void	testPrefixAndMalformed(void)
{
	Message	msg;

	// Clients do not normally send a prefix — the server adds one when it
	// relays — but it still has to be skipped, or ":nick!u@h" becomes the
	// command and the real one is lost.
	msg = parseMessage(":nick!u@h PRIVMSG #chan :hi");
	checkEqual(msg.prefix, "nick!u@h",
		"prefix: extracted without its leading colon");
	checkEqual(msg.command, "PRIVMSG",
		"prefix: the command is what follows it, not the prefix itself");

	msg = parseMessage("PING");
	checkEqual(msg.command, "PING",
		"grammar: a command may stand alone");
	check(msg.params.empty(),
		"grammar: and then it carries no params");

	// Malformed yields an empty command and the caller ignores the line in
	// silence. extractCommand really does produce empty lines — "\r\n" alone
	// is a complete, empty line — so replying would be answering noise.
	check(parseMessage("").command.empty(),
		"malformed: an empty line has no command");
	check(parseMessage("   ").command.empty(),
		"malformed: a line of nothing but spaces has no command");
	check(parseMessage(":onlyprefix").command.empty(),
		"malformed: a prefix with no command is ignored");

	// The parser reports what was on the wire. Uppercasing belongs to dispatch,
	// so that 421 can echo the command back as the user actually typed it.
	checkEqual(parseMessage("privmsg #chan :hi").command, "privmsg",
		"grammar: the command keeps the case it arrived in");

	// Repeated spaces are separators, not content.
	msg = parseMessage("MODE  #chan   +o");
	check(msg.params.size() == 2,
		"grammar: repeated spaces do not invent empty params");
}

static void	testTrailingRule(void)
{
	Message	fresh;
	Message	msg;

	check(!fresh.hasTrailing,
		"trailing: a default-constructed Message has none");

	// ":" alone is a legal EMPTY trailing, which is the whole reason
	// hasTrailing exists: "last param is empty" cannot also mean "no trailing".
	// PRIVMSG #chan : is 412 ERR_NOTEXTTOSEND, PRIVMSG #chan is 411.
	msg = parseMessage("PRIVMSG #chan :");
	check(msg.hasTrailing,
		"trailing: a lone colon still counts as present");
	check(msg.params.size() == 2 && msg.params[1].empty(),
		"trailing: and it arrives as an empty last param");

	msg = parseMessage("PRIVMSG #chan");
	check(!msg.hasTrailing,
		"trailing: absent when no colon was sent");
	check(msg.params.size() == 1,
		"trailing: so only the middle param is there");

	// A colon that does not start a param is ordinary text.
	msg = parseMessage("PRIVMSG #chan a:b");
	check(msg.params.size() == 2 && msg.params[1] == "a:b",
		"trailing: a mid-token colon is not a marker");
	check(!msg.hasTrailing,
		"trailing: and it does not raise the flag");

	// After a real marker, everything is content — spaces and later colons.
	msg = parseMessage("PRIVMSG #chan :a  b");
	checkEqual(msg.params[1], "a  b",
		"trailing: interior spaces are preserved exactly");
	msg = parseMessage("PRIVMSG #chan :see http://x for more");
	checkEqual(msg.params[1], "see http://x for more",
		"trailing: a later colon is just text");
}

void	runMessageTests(void)
{
	testArchitectureExamples();
	testPrefixAndMalformed();
	testTrailingRule();
}
