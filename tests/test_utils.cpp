#include "Utils.hpp"
#include "Limits.hpp"

#include "harness.hpp"

#include <climits>

// Unit tests for the shared middle. Pure functions, hand-built strings, no
// state anywhere — the easiest things in the project to test, and the ones both
// tracks depend on.

static void	testCasemapping(void)
{
	// The README's example, and the single case that plain std::tolower gets
	// wrong.
	checkEqual(utils::toIrcLower("Nick[42]"), "nick{42}",
		"casemapping: [ and ] lower to { and }");

	// All four pairs at once, RFC 2812 section 2.2.
	checkEqual(utils::toIrcLower("[]\\~"), "{}|^",
		"casemapping: the four bracket pairs all map");

	// Already-lowered input is left alone, so the function is idempotent. It
	// builds comparison keys, which have to be stable under re-application.
	checkEqual(utils::toIrcLower("{}|^"), "{}|^",
		"casemapping: lowering an already lowered string changes nothing");

	checkEqual(utils::toIrcLower("ALICE"), "alice",
		"casemapping: ordinary letters still lower normally");
	checkEqual(utils::toIrcLower("nick-42_"), "nick-42_",
		"casemapping: digits and punctuation are untouched");

	// Bytes >= 0x80 reach nicknames, because extraction strips only NUL, CR
	// and LF. They must survive intact — and this is exactly the input where
	// handing a negative char to std::tolower would have been undefined.
	std::string	utf8;

	utf8 += static_cast<char>(0xC3);
	utf8 += static_cast<char>(0xA9);
	checkEqual(utils::toIrcLower(utf8), utf8,
		"casemapping: bytes above 0x7F pass through unchanged");
}

static void	testEqualsIgnoreCase(void)
{
	// The collision a std::tolower implementation would miss. To IRC these
	// are the SAME nickname, so the second must be refused with 433.
	check(utils::equalsIgnoreCase("Nick[42]", "nick{42}"),
		"equalsIgnoreCase: bracket pairs count as the same nickname");
	check(utils::equalsIgnoreCase("ALICE", "alice"),
		"equalsIgnoreCase: ordinary case differences match");
	check(!utils::equalsIgnoreCase("alice", "alicia"),
		"equalsIgnoreCase: different content does not match");
	check(!utils::equalsIgnoreCase("alice", "alice2"),
		"equalsIgnoreCase: different lengths do not match");
	check(utils::equalsIgnoreCase("", ""),
		"equalsIgnoreCase: two empty strings match");
}

static void	testSplit(void)
{
	std::vector<std::string>	fields;

	// Empty fields are preserved. IRC matches parallel lists positionally, so
	// collapsing one shifts every later key onto the wrong channel.
	fields = utils::split("#a,,#b", ',');
	check(fields.size() == 3,
		"split: an empty field between two values is kept");
	check(fields[1].empty(),
		"split: and that middle field really is empty");

	fields = utils::split("#a", ',');
	check(fields.size() == 1,
		"split: a string with no delimiter is a single field");

	// The decision, pinned: (delimiters + 1) fields, always. "JOIN :" reaches
	// this, and one empty field flows into validation and earns a 403 —
	// zero fields would answer a real command with silence.
	fields = utils::split("", ',');
	check(fields.size() == 1 && fields[0].empty(),
		"split: an empty string is one empty field, not zero fields");

	fields = utils::split(",", ',');
	check(fields.size() == 2,
		"split: a lone delimiter yields two empty fields");

	// The alignment the whole policy exists to protect: "JOIN #a,#b ,key2"
	// must leave the key list the same length as the channel list.
	check(utils::split("#a,#b", ',').size() == utils::split(",key2", ',').size(),
		"split: channel and key lists stay positionally aligned");
}

static void	testToString(void)
{
	checkEqual(utils::toString(0), "0",
		"toString: zero");
	checkEqual(utils::toString(-42), "-42",
		"toString: a negative value keeps its sign");
	// INT_MIN is where a hand-rolled version breaks: negating it is undefined,
	// because +2147483648 does not fit in an int.
	checkEqual(utils::toString(INT_MIN), "-2147483648",
		"toString: INT_MIN survives");
}

static void	testParseInt(void)
{
	int	value = 0;

	check(utils::parseInt("42", value) && value == 42,
		"parseInt: a plain number parses");
	check(utils::parseInt("-5", value) && value == -5,
		"parseInt: a negative number parses");

	// atoi returns 12 for the first of these and 0 for the second, and that 0
	// is indistinguishable from a legitimate zero. That is why this exists.
	check(!utils::parseInt("12x", value),
		"parseInt: trailing garbage is refused");
	check(!utils::parseInt("abc", value),
		"parseInt: a non-number is refused");
	check(!utils::parseInt("", value),
		"parseInt: an empty string is refused");
	check(!utils::parseInt("-", value),
		"parseInt: a lone sign is refused");

	check(utils::parseInt("2147483647", value) && value == INT_MAX,
		"parseInt: INT_MAX is accepted");
	check(!utils::parseInt("2147483648", value),
		"parseInt: one past INT_MAX overflows and is refused");
}

static void	testIsValidNickname(void)
{
	// The README's pair.
	check(utils::isValidNickname("alice"),
		"nickname: a plain name is valid");
	check(!utils::isValidNickname("4lice"),
		"nickname: it cannot start with a digit");

	check(!utils::isValidNickname(""),
		"nickname: empty is not a name");
	// RFC 2812 special: [ \ ] ^ _ ` { | } are legal, and legal FIRST.
	check(utils::isValidNickname("[alice]"),
		"nickname: a special character may lead");
	check(utils::isValidNickname("a-b"),
		"nickname: a hyphen is legal after the first character");
	check(!utils::isValidNickname("-alice"),
		"nickname: but a hyphen may not lead");
	check(!utils::isValidNickname("ali ce"),
		"nickname: a space is not allowed");
	// ~ is 0x7E, outside %x7B-7D, so it never appears in a legal nickname —
	// which is why the ~/^ casemapping direction only matters for channels.
	check(!utils::isValidNickname("nick~"),
		"nickname: ~ is not in the RFC's special set");

	check(utils::isValidNickname(std::string(irc::MAX_NICKNAME_LEN, 'a')),
		"nickname: exactly 30 characters is accepted");
	check(!utils::isValidNickname(std::string(irc::MAX_NICKNAME_LEN + 1, 'a')),
		"nickname: 31 is one too many");
}

static void	testIsValidChannelName(void)
{
	check(utils::isValidChannelName("#test"),
		"channel: a plain # channel is valid");
	check(!utils::isValidChannelName("test"),
		"channel: a name with no prefix is not a channel");
	check(!utils::isValidChannelName("#"),
		"channel: the prefix on its own is not a channel");
	// Decision: # only. & means server-local, and with no server links every
	// channel here is already exactly that.
	check(!utils::isValidChannelName("&test"),
		"channel: & is not accepted as a prefix");

	check(!utils::isValidChannelName("#a b"),
		"channel: a space is not allowed");
	check(!utils::isValidChannelName("#a,b"),
		"channel: a comma would split the name inside a JOIN list");
	check(!utils::isValidChannelName("#a:b"),
		"channel: a colon would start a trailing parameter");

	check(utils::isValidChannelName("#"
			+ std::string(irc::MAX_CHANNEL_LEN - 1, 'a')),
		"channel: exactly 50 characters is accepted");
	check(!utils::isValidChannelName("#"
			+ std::string(irc::MAX_CHANNEL_LEN, 'a')),
		"channel: 51 is one too many");
}

void	runUtilsTests(void)
{
	testCasemapping();
	testEqualsIgnoreCase();
	testSplit();
	testToString();
	testParseInt();
	testIsValidNickname();
	testIsValidChannelName();
}
