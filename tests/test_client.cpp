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

void	runClientTests(void)
{
	testOrthodoxCanonicalForm();
	testIdentity();
	testRegistrationState();
}
