#include "Utils.hpp"
#include "Limits.hpp"

#include <climits>
#include <sstream>

// Shared middle: pure helpers owned by neither track. No fds, no sockets, no
// globals. See include/Utils.hpp for the contract.

// --- IRC casemapping (RFC 2812 section 2.2) -------------------------------
//
// [ ] \ ~ are uppercase; { } | ^ are their lowercase forms. That is less a
// quirk of the protocol than a fossil inside it. IRC was written in Finland in
// 1988, when machines still used national variants of ASCII, and in the
// Finnish/Swedish variant the bytes plain ASCII spends on brackets held letters
// instead: 0x5B/0x5C/0x5D were Ä/Ö/Å and 0x7B/0x7C/0x7D were ä/ö/å. On those
// terminals [ and { really were one letter in two cases. The world moved to
// Unicode; the protocol did not.
//
// The arithmetic falls out of the same fact. ASCII case is a single 0x20 bit,
// A-Z runs 0x41-0x5A, and [ \ ] sit immediately after at 0x5B-0x5D, so ONE
// range covers all of them. ~ -> ^ is the exception because it shifts down
// rather than up; that direction is fixed by Utils.hpp and
// docs/ARCHITECTURE.md section 5, and both tracks depend on it agreeing.
//
// std::tolower is deliberately not used here. It knows nothing about the
// bracket pairs, so these branches would exist regardless, and it carries two
// problems of its own: it takes an int and is UNDEFINED for a negative char —
// reachable in this server, since Client::extractCommand strips only NUL, CR
// and LF, so bytes >= 0x80 do arrive inside nicknames — and it consults the
// global C locale, where tolower('I') is not always 'i'. IRC casemapping is
// defined on ASCII alone, so no locale belongs in this answer.
//
// Getting this wrong is not cosmetic. Nickname collisions are checked with
// equalsIgnoreCase and refused with 433 (docs/ARCHITECTURE.md section 7). Miss
// the bracket pairs and Nick[42] and nick{42} — the SAME nickname to IRC — both
// register successfully, the 433 never fires, and a message addressed to either
// reaches whichever one the lookup happens to find first.
static char	toIrcLowerChar(char c)
{
	if (c >= 'A' && c <= ']')
		return (static_cast<char>(c + 32));
	if (c == '~')
		return ('^');
	return (c);
}

std::string	utils::toIrcLower(const std::string &s)
{
	std::string				lowered(s);
	std::string::size_type	i;

	for (i = 0; i < lowered.size(); ++i)
		lowered[i] = toIrcLowerChar(lowered[i]);
	return (lowered);
}

// Compares two strings under the casemapping above without building either
// lowered form.
//
// toIrcLower(a) == toIrcLower(b) would be shorter and allocates two strings for
// a comparison that usually dies on the first character. The length check
// rejects most mismatches before a single character is looked at, and this runs
// on every nickname lookup.
bool	utils::equalsIgnoreCase(const std::string &a, const std::string &b)
{
	std::string::size_type	i;

	if (a.size() != b.size())
		return (false);
	for (i = 0; i < a.size(); ++i)
	{
		if (toIrcLowerChar(a[i]) != toIrcLowerChar(b[i]))
			return (false);
	}
	return (true);
}

// --- splitting ------------------------------------------------------------
//
// Empty fields are PRESERVED, and not out of pedantry: IRC matches parallel
// lists positionally.
//
//     JOIN #a,#b key1,key2     key1 belongs to #a, key2 to #b
//     JOIN #a,#b ,key2         #a has no key at all, #b uses key2
//
// The second form is legal and real clients send it. Collapse that empty field
// and the key list becomes ["key2"], which then lands on #a — the wrong
// channel. The user is refused entry to one channel and hands a key to another.
//
// The invariant is one line: the result always holds (delimiters + 1) fields.
// That is why split("", delim) returns ONE EMPTY FIELD rather than none — no
// delimiters, so one field — and it is also the better behaviour downstream.
// "JOIN :" reaches split(""), where one empty field flows into
// isValidChannelName, fails, and earns the client a 403. Zero fields would skip
// the loop body entirely and answer a command the client really sent with
// silence.
std::vector<std::string>	utils::split(const std::string &s, char delim)
{
	std::vector<std::string>	fields;
	std::string::size_type		start = 0;
	std::string::size_type		pos;

	while ((pos = s.find(delim, start)) != std::string::npos)
	{
		fields.push_back(s.substr(start, pos - start));
		start = pos + 1;
	}
	fields.push_back(s.substr(start));
	return (fields);
}

// --- numeric conversion ---------------------------------------------------

// C++98 has no std::to_string.
//
// Not sprintf: that needs a hand-sized buffer, and the value it has to survive
// is INT_MIN — eleven characters plus the terminator. A stream has no buffer to
// size wrongly and no format string to mismatch against its argument.
std::string	utils::toString(int value)
{
	std::ostringstream	stream;

	stream << value;
	return (stream.str());
}

// Refuses rather than guesses, which atoi cannot do: atoi("12x") is 12, and
// atoi("abc") is 0 — indistinguishable from a legitimate "0". Its behaviour on
// overflow is undefined. The argument here comes straight off the wire
// (MODE +l <limit>), so "that is not a number" has to be sayable.
//
// Hence bool plus an out-parameter, the same idiom as Client::extractCommand
// and for the same reason: failure and a legal value must stay distinguishable.
//
// The overflow test runs BEFORE the multiply. Checking afterwards is the usual
// mistake, and it is undefined behaviour — signed overflow has already happened
// by the time there is anything left to inspect.
//
// A leading + or - is accepted, because this answers "is this an integer";
// whether a negative value is MEANINGFUL belongs to the caller, and cmdMode
// refuses a non-positive limit itself. INT_MIN is refused as overflow, since an
// int accumulator cannot hold its magnitude — refused explicitly rather than
// wrapped silently, and nothing in this project has a use for it.
bool	utils::parseInt(const std::string &s, int &out)
{
	std::string::size_type	i = 0;
	bool					negative = false;
	int						value = 0;
	int						digit;

	if (s.empty())
		return (false);
	if (s[0] == '+' || s[0] == '-')
	{
		negative = (s[0] == '-');
		i = 1;
	}
	if (i == s.size())
		return (false);
	while (i < s.size())
	{
		if (s[i] < '0' || s[i] > '9')
			return (false);
		digit = s[i] - '0';
		if (value > (INT_MAX - digit) / 10)
			return (false);
		value = value * 10 + digit;
		++i;
	}
	out = (negative ? -value : value);
	return (true);
}

// --- validation -----------------------------------------------------------
//
// The bool is the whole answer. No error strings, because the caller is the one
// that knows whether a rejection means 432 (bad nickname) or 403 (bad channel).

// RFC 2812 section 2.3.1 "special": %x5B-60 is [ \ ] ^ _ ` and %x7B-7D is
// { | }. Note that ~ (0x7E) is NOT in that set, so a legal nickname can never
// contain one.
static bool	isNickSpecial(char c)
{
	return ((c >= '[' && c <= '`') || (c >= '{' && c <= '}'));
}

static bool	isLetter(char c)
{
	return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}

static bool	isDigit(char c)
{
	return (c >= '0' && c <= '9');
}

// nickname = ( letter / special ) *( letter / digit / special / "-" )
//
// A digit or a '-' may appear anywhere except first. The RFC caps the whole
// name at 9 characters; we use irc::MAX_NICKNAME_LEN (30) instead, because
// irssi takes the nickname from the system username and 9 would reject a lot of
// real logins on sight. See Limits.hpp.
//
// Bytes >= 0x80 fail every range test under either char signedness, so
// non-ASCII is rejected rather than being undefined.
bool	utils::isValidNickname(const std::string &nickname)
{
	std::string::size_type	i;

	if (nickname.empty() || nickname.size() > irc::MAX_NICKNAME_LEN)
		return (false);
	if (!isLetter(nickname[0]) && !isNickSpecial(nickname[0]))
		return (false);
	for (i = 1; i < nickname.size(); ++i)
	{
		if (!isLetter(nickname[i]) && !isDigit(nickname[i])
			&& !isNickSpecial(nickname[i]) && nickname[i] != '-')
			return (false);
	}
	return (true);
}

// '#' only, though RFC 2812 also lists &, + and !.
//
// '+' means "supports no modes", which contradicts the MODE work the subject
// requires, and '!' needs generated channel IDs. '&' means "server-local" — and
// since the subject forbids server-to-server links, every channel here is
// already local, so '&' would be a second prefix with identical behaviour.
//
// After the '#': any byte except NUL, BELL, space, comma and colon. A comma
// would split the name inside a JOIN list; a colon would start a trailing
// parameter. CR and LF are already gone by extraction, so rejecting them here
// is belt-and-braces.
bool	utils::isValidChannelName(const std::string &name)
{
	std::string::size_type	i;

	if (name.size() < 2 || name.size() > irc::MAX_CHANNEL_LEN)
		return (false);
	if (name[0] != '#')
		return (false);
	for (i = 1; i < name.size(); ++i)
	{
		if (name[i] == ' ' || name[i] == ',' || name[i] == ':'
			|| name[i] == '\0' || name[i] == '\a'
			|| name[i] == '\r' || name[i] == '\n')
			return (false);
	}
	return (true);
}
