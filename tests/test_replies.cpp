#include "Replies.hpp"

#include "harness.hpp"

// Both builders are pure string assembly. The expected strings here are taken
// from docs/ARCHITECTURE.md section 6; if they ever disagree, the doc wins.

static void	testNumeric(void)
{
	// The padding this function exists for. RPL_WELCOME is the int 1 and must
	// leave as 001 — RFC 2812 section 2.4 makes a numeric three digits, so
	// "1" is not recognised as a numeric and irssi waits for a welcome that
	// never parses.
	checkEqual(irc::numeric("irc.local", irc::RPL_WELCOME, "*", ":Welcome"),
		":irc.local 001 * :Welcome",
		"numeric: code 1 goes out as 001");
	checkEqual(irc::numeric("irc.local", irc::RPL_YOURHOST, "*", ":Your host"),
		":irc.local 002 * :Your host",
		"numeric: code 2 goes out as 002");
	checkEqual(irc::numeric("irc.local", irc::RPL_CREATED, "*", ":Created"),
		":irc.local 003 * :Created",
		"numeric: code 3 goes out as 003");
	checkEqual(irc::numeric("irc.local", irc::RPL_MYINFO, "*", "irc.local 1 o itkl"),
		":irc.local 004 * irc.local 1 o itkl",
		"numeric: code 4 goes out as 004");

	// Not a special case: 421 simply already has three digits.
	checkEqual(irc::numeric("irc.local", irc::ERR_UNKNOWNCOMMAND, "alice",
			"FOO :Unknown command"),
		":irc.local 421 alice FOO :Unknown command",
		"numeric: a three-digit code passes through unchanged");

	// target is the recipient's nickname once registered, "*" before that.
	checkEqual(irc::numeric("irc.local", irc::ERR_NOTREGISTERED, "*",
			":You have not registered"),
		":irc.local 451 * :You have not registered",
		"numeric: target is * before registration");
	checkEqual(irc::numeric("irc.local", irc::ERR_NICKNAMEINUSE, "alice",
			"bob :Nickname is already in use"),
		":irc.local 433 alice bob :Nickname is already in use",
		"numeric: target is the recipient's nick after registration");

	// sendToClient owns the CRLF. Appending it here too sends a blank line.
	check(irc::numeric("irc.local", irc::RPL_WELCOME, "*", ":Welcome")
			.find('\r') == std::string::npos,
		"numeric: no CR is appended");
	check(irc::numeric("irc.local", irc::RPL_WELCOME, "*", ":Welcome")
			.find('\n') == std::string::npos,
		"numeric: no LF is appended");

	// Empty args must not leave a dangling space before that CRLF, which a
	// strict parser would read as an extra empty parameter.
	checkEqual(irc::numeric("irc.local", irc::RPL_WELCOME, "*", ""),
		":irc.local 001 *",
		"numeric: empty args leave no trailing space");
}

static void	testFromClient(void)
{
	checkEqual(irc::fromClient("a!b@c", "JOIN", "#x"),
		":a!b@c JOIN #x",
		"fromClient: prefix, command and args");
	// The ':' of a trailing param belongs to the caller's args, since only the
	// caller knows which argument is the trailing one.
	checkEqual(irc::fromClient("nick!u@h", "PRIVMSG", "#chan :hello world"),
		":nick!u@h PRIVMSG #chan :hello world",
		"fromClient: the trailing colon comes from the caller");
	checkEqual(irc::fromClient("nick!u@h", "QUIT", ""),
		":nick!u@h QUIT",
		"fromClient: empty args leave no trailing space");
	check(irc::fromClient("a!b@c", "JOIN", "#x").find('\n') == std::string::npos,
		"fromClient: no CRLF is appended");
}

void	runRepliesTests(void)
{
	testNumeric();
	testFromClient();
}
