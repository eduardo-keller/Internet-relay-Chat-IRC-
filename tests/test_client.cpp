#include "Client.hpp"
#include "Limits.hpp"

#include "harness.hpp"

// Unit tests for the transport track's Client. Registration flags, read-buffer
// reassembly, the output queue and the deferred-disconnect flag.
//
// No socket is opened anywhere in this file: every input is a hand-built
// string. That is the whole point of the seam — see docs/ARCHITECTURE.md
// section 1.

static void	testOrthodoxCanonicalForm(void)
{
	Client	defaulted;

	check(defaulted.getHostname().empty(),
		"OCF: default client has an empty hostname");

	Client	original(3, "localhost");

	checkEqual(original.getHostname(), "localhost",
		"OCF: constructor stores the hostname");

	Client	copied(original);

	checkEqual(copied.getHostname(), "localhost",
		"OCF: copy constructor carries the state over");

	Client	assigned;

	assigned = original;
	checkEqual(assigned.getHostname(), "localhost",
		"OCF: assignment carries the state over");

	// Self-assignment must be a no-op, not a corruption. Going through an
	// alias instead of writing `assigned = assigned` is both how the case
	// actually shows up in real code and a way around the compiler's
	// self-assignment warning, which -Werror would turn into a build failure.
	Client	*alias = &assigned;

	assigned = *alias;
	checkEqual(assigned.getHostname(), "localhost",
		"OCF: self-assignment leaves the object intact");
}

static void	testIdentity(void)
{
	Client	client(3, "localhost");

	check(client.getNickname().empty(),
		"identity: a fresh client has no nickname yet");
	check(client.getUsername().empty(),
		"identity: a fresh client has no username yet");

	client.setNickname("alice");
	client.setUsername("alice");
	client.setRealname("Alice Liddell");

	checkEqual(client.getNickname(), "alice",
		"identity: setNickname round-trips");
	// The realname arrives as USER's trailing parameter, so it legitimately
	// contains spaces. Nothing here may split on whitespace.
	checkEqual(client.getRealname(), "Alice Liddell",
		"identity: realname keeps its spaces");

	checkEqual(client.prefix(), "alice!alice@localhost",
		"identity: prefix is nick!user@host");

	// A NICK change must be visible in the very next prefix: every JOIN /
	// PRIVMSG echo the channel sees is rebuilt from it. This is the test a
	// cached prefix would fail.
	client.setNickname("bob");
	checkEqual(client.prefix(), "bob!alice@localhost",
		"identity: prefix follows a nick change");
}

static void	testRegistrationState(void)
{
	Client	client(3, "localhost");

	check(!client.isRegistered(),
		"registration: a fresh client is not registered");

	client.setHasPass(true);
	check(!client.isRegistered(), "registration: PASS alone is not enough");

	client.setHasNick(true);
	check(!client.isRegistered(), "registration: PASS + NICK is not enough");

	client.setHasUser(true);
	check(client.isRegistered(), "registration: PASS + NICK + USER registers");

	// The three flags are independent bits, not a counter or a step index:
	// whichever one is cleared, the client stops being registered.
	client.setHasNick(false);
	check(!client.isRegistered(),
		"registration: clearing any single flag unregisters");

	// Order does not matter. irssi sends PASS, NICK and USER back to back in
	// one packet (ARCHITECTURE.md section 7), and nothing guarantees the
	// dispatch order matches the RFC's presentation order.
	Client	other(4, "localhost");

	other.setHasUser(true);
	other.setHasNick(true);
	other.setHasPass(true);
	check(other.isRegistered(),
		"registration: the order the flags arrive in does not matter");

	// welcomeSent is a separate question: being registered says nothing about
	// whether the 001-004 burst already went out.
	check(!other.welcomeSent(),
		"registration: registered does not imply the welcome was sent");
	other.setWelcomeSent(true);
	check(other.welcomeSent() && other.isRegistered(),
		"registration: welcomeSent and isRegistered are independent");
}

// The subject's own test, chapter III.3: three separate writes, one command.
// This is the single scenario the whole transport track exists to handle.
static void	testPartialPacketReassembly(void)
{
	Client		client(3, "localhost");
	std::string	out;

	check(!client.extractCommand(out),
		"read buffer: an empty buffer holds no complete line");

	check(client.appendToReadBuffer("com"),
		"read buffer: a fragment is accepted");
	check(!client.extractCommand(out),
		"read buffer: an unterminated fragment is not a command");

	client.appendToReadBuffer("man");
	check(!client.extractCommand(out),
		"read buffer: two fragments are still not a command");

	client.appendToReadBuffer("d\r\n");
	check(client.extractCommand(out),
		"read buffer: the terminator completes the line");
	checkEqual(out, "command",
		"read buffer: the three fragments rebuild 'command'");

	check(!client.extractCommand(out),
		"read buffer: extracting drains the line out of the buffer");
}

// The other half of the same problem. TCP coalesces as readily as it
// fragments, and irssi leans on that: PASS, NICK and USER go out back to back
// and often land in one packet (docs/ARCHITECTURE.md section 7).
static void	testLineFraming(void)
{
	Client		client(3, "localhost");
	std::string	out;

	client.appendToReadBuffer("PASS secret\r\nNICK alice\r\n");
	check(client.extractCommand(out),
		"framing: the first of two coalesced lines comes out");
	checkEqual(out, "PASS secret",
		"framing: one append can carry two commands");
	check(client.extractCommand(out),
		"framing: the second line comes out on the next call");
	checkEqual(out, "NICK alice",
		"framing: the second command survives intact");
	check(!client.extractCommand(out),
		"framing: nothing is left behind after both are drained");

	// nc without -C terminates with a bare LF, and the subject's test is
	// written with nc. A server that insists on CRLF never answers it.
	client.appendToReadBuffer("USER alice 0 * :Alice\n");
	check(client.extractCommand(out),
		"framing: a bare LF terminates a line just like CRLF");
	checkEqual(out, "USER alice 0 * :Alice",
		"framing: a bare-LF line arrives with no stray CR attached");

	// A blank line is a COMPLETE line, not an absent one. This is the case
	// that forces the bool/out-parameter split: "" cannot also mean
	// "nothing available", because the caller must react differently.
	client.appendToReadBuffer("\r\n");
	check(client.extractCommand(out),
		"framing: a lone CRLF is a complete, empty line");
	check(out.empty(),
		"framing: that line is empty, and the dispatcher ignores it silently");
}

// RFC 2812 section 2.3: 512 bytes including the CRLF, so 510 of payload. The
// policy is truncate and PROCESS, not disconnect — the line is complete and the
// client is only verbose, and no real server drops a connection over that.
static void	testLineTruncation(void)
{
	Client		client(3, "localhost");
	std::string	out;

	client.appendToReadBuffer(std::string(600, 'a') + "\r\n");
	check(client.extractCommand(out),
		"truncation: an over-long line is still a complete line");
	check(out.size() == irc::MAX_PAYLOAD_LEN,
		"truncation: 600 bytes of payload are cut down to 510");
	// The 90 discarded bytes must be GONE, not left in the buffer, or they
	// would come back as a second command assembled out of the tail.
	check(!client.extractCommand(out),
		"truncation: the discarded tail does not become a second command");

	// One under, one exactly at, one over: the boundary is where an
	// off-by-one would live.
	client.appendToReadBuffer(std::string(irc::MAX_PAYLOAD_LEN - 1, 'b')
		+ "\r\n");
	check(client.extractCommand(out)
		&& out == std::string(irc::MAX_PAYLOAD_LEN - 1, 'b'),
		"truncation: 509 bytes are left alone");

	client.appendToReadBuffer(std::string(irc::MAX_PAYLOAD_LEN, 'c') + "\r\n");
	check(client.extractCommand(out)
		&& out == std::string(irc::MAX_PAYLOAD_LEN, 'c'),
		"truncation: exactly 510 passes through untouched");

	client.appendToReadBuffer(std::string(irc::MAX_PAYLOAD_LEN + 1, 'd')
		+ "\r\n");
	check(client.extractCommand(out) && out.size() == irc::MAX_PAYLOAD_LEN,
		"truncation: 511 comes back as 510");
}

// Nobody legitimate sends these bytes. irssi never does; nc with a file piped
// into it does, and so does an evaluator dumping binary at the socket to check
// the subject's "must not crash in any circumstances" rule.
static void	testSanitising(void)
{
	Client		client(3, "localhost");
	std::string	out;

	// std::string holds a NUL fine, .c_str() stops at it, and both are in the
	// same program: 12 bytes to size(), 4 to anything C-shaped. Left in, the
	// dispatch lookup misses and answers 421 while a debug print says "PING".
	std::string	withNul("PING");

	withNul += '\0';
	withNul += " :token";
	client.appendToReadBuffer(withNul + "\r\n");
	check(client.extractCommand(out),
		"sanitising: a line carrying a NUL still extracts");
	checkEqual(out, "PING :token",
		"sanitising: the NUL is stripped out of the line");

	// A bare CR in the middle survives the terminator strip. It jumps the
	// recipient's cursor to column 0 and desyncs any parser lenient enough to
	// treat CR alone as a line ending.
	client.appendToReadBuffer("PRIVMSG #chan :hello\rworld\r\n");
	check(client.extractCommand(out),
		"sanitising: a line with an embedded CR still extracts");
	checkEqual(out, "PRIVMSG #chan :helloworld",
		"sanitising: the embedded CR is stripped");

	// ...but we strip ONLY those three. 0x01 delimits CTCP, which is how
	// irssi sends /me. The literal is split because "\x01A" would otherwise
	// be read as the hex escape \x1A.
	std::string	ctcp("PRIVMSG #chan :\x01" "ACTION waves\x01");

	client.appendToReadBuffer(ctcp + "\r\n");
	check(client.extractCommand(out),
		"sanitising: a CTCP line extracts");
	checkEqual(out, ctcp,
		"sanitising: CTCP's 0x01 survives, so /me keeps working");

	// Every byte value at once, minus the LF that would frame it early: the
	// evaluator piping /dev/urandom. 255 bytes in, NUL and CR out, 253 left.
	std::string	binary;

	for (int i = 0; i < 256; ++i)
	{
		if (static_cast<char>(i) != '\n')
			binary += static_cast<char>(i);
	}
	client.appendToReadBuffer(binary + "\r\n");
	check(client.extractCommand(out),
		"sanitising: binary garbage does not break extraction");
	check(out.find('\0') == std::string::npos
		&& out.find('\r') == std::string::npos
		&& out.find('\n') == std::string::npos,
		"sanitising: no NUL, CR or LF survives extraction");
	check(out.size() == binary.size() - 2,
		"sanitising: exactly two bytes go, so nothing else is over-stripped");

	// The accepted consequence of truncating BEFORE cleaning: the delivered
	// line can be shorter than 510. Anchoring the cap to bytes on the wire is
	// what stops NUL padding from smuggling payload past the limit.
	std::string	padded(irc::MAX_PAYLOAD_LEN, 'a');

	padded[10] = '\0';
	padded[20] = '\0';
	padded[30] = '\0';
	client.appendToReadBuffer(padded + "\r\n");
	check(client.extractCommand(out),
		"sanitising: a NUL-padded line at the cap extracts");
	check(out.size() == irc::MAX_PAYLOAD_LEN - 3,
		"sanitising: cleaning after truncating can leave under 510");
}

// The cap exists because an unbounded read buffer is memory exhaustion
// reachable by one client that looks perfectly polite. The QUALIFIER is what
// the step is really about: past the cap with no complete line is a flood, past
// the cap with complete lines is pipelining and must not be punished.
static void	testReadBufferCap(void)
{
	std::string	out;
	Client		flooder(3, "localhost");

	check(!flooder.appendToReadBuffer(std::string(5000, 'x')),
		"read cap: 5000 bytes with no terminator are refused");

	// THE test of this step. The same 5000 bytes are fine when a line is
	// finished inside them — that is legitimate pipelining, which is exactly
	// what an evaluator pasting a block of commands produces.
	Client	pipeliner(4, "localhost");

	check(pipeliner.appendToReadBuffer(std::string(2000, 'x') + "\r\n"
			+ std::string(3000, 'y')),
		"read cap: 5000 bytes WITH a complete line inside are accepted");
	check(pipeliner.extractCommand(out),
		"read cap: and that complete line still drains normally");

	// The cap has to be exceeded, not merely reached.
	Client	atLimit(5, "localhost");

	check(atLimit.appendToReadBuffer(std::string(irc::MAX_READ_BUFFER, 'x')),
		"read cap: exactly 4096 unterminated bytes still pass");

	Client	overLimit(6, "localhost");

	check(!overLimit.appendToReadBuffer(
			std::string(irc::MAX_READ_BUFFER + 1, 'x')),
		"read cap: one byte over the cap is refused");

	// The bound is on the accumulated buffer, not on one chunk: a client
	// dripping unterminated bytes trips it just like one big write does.
	Client	dripper(7, "localhost");
	bool	accepted = true;

	for (int i = 0; i < 10 && accepted; ++i)
		accepted = dripper.appendToReadBuffer(std::string(1000, 'z'));
	check(!accepted,
		"read cap: an unterminated drip accumulates until it trips the cap");
}

static void	testDeferredDisconnect(void)
{
	Client	client(3, "localhost");

	check(!client.isDisconnecting(),
		"disconnect: a fresh client is not disconnecting");

	client.markDisconnecting("Password incorrect");
	check(client.isDisconnecting(),
		"disconnect: marking raises the flag");
	// The reason is not decoration: it is the payload of the
	// "ERROR :<reason>" line the reap still has to send before close().
	checkEqual(client.getQuitReason(), "Password incorrect",
		"disconnect: the reason is kept for the ERROR line");

	// The point of the whole step. A second mark is the CONSEQUENCE of the
	// first (a short send, a recv returning 0, all downstream of the client
	// already being dropped), so letting it win would report the symptom and
	// throw away the cause.
	client.markDisconnecting("Client exited");
	checkEqual(client.getQuitReason(), "Password incorrect",
		"disconnect: a second mark does not overwrite the first reason");
}

void	runClientTests(void)
{
	testOrthodoxCanonicalForm();
	testIdentity();
	testRegistrationState();
	testPartialPacketReassembly();
	testLineFraming();
	testLineTruncation();
	testSanitising();
	testReadBufferCap();
	testDeferredDisconnect();
}
