#include "Replies.hpp"
#include "Utils.hpp"

// The two output shapes, and the only place either is built. See
// docs/ARCHITECTURE.md section 6 for the formats and the numerics table.
//
// NEITHER function appends CRLF. Server::sendToClient owns the "\r\n"
// (ARCHITECTURE section 2), so adding it here as well would put a blank line
// after every reply — which confuses irssi rather than being merely untidy.

// Three digits, always.
//
// RFC 2812 section 2.4 defines a numeric reply as a three-digit number, so
// RPL_WELCOME — the int 1 — has to leave as "001". Send "1" and irssi does not
// recognise the line as a numeric at all: it never sees the welcome and sits
// waiting for one forever. This is the bug the function exists to prevent.
//
// Padding is not a special case for small codes. It is the same operation for
// every code, and a value like 421 simply already has three digits.
//
// Built on utils::toString rather than <iomanip> so the project keeps a single
// integer-to-string path — and because setw() applies only to the next output
// while setfill() is sticky, a distinction not worth depending on here.
//
// Codes are assumed to be the constants in Replies.hpp, i.e. 1..999. Nothing
// validates that: a negative or four-digit code would produce nonsense, and
// inventing codes is already forbidden by the header.
static std::string	formatCode(int code)
{
	std::string	digits = utils::toString(code);

	while (digits.size() < 3)
		digits = "0" + digits;
	return (digits);
}

// ":<serverName> <code> <target> <args>"
//
// `target` is the recipient's nickname, or "*" before they have registered.
// `args` arrives already carrying the ':' of a trailing parameter, because only
// the caller knows which of its arguments is the trailing one.
//
// args is appended only when non-empty. Otherwise the line would end in a space
// just before sendToClient's CRLF, which a strict parser reads as an extra
// empty parameter.
std::string	irc::numeric(const std::string &serverName, int code,
	const std::string &target, const std::string &args)
{
	std::string	line;

	line = ":" + serverName + " " + formatCode(code) + " " + target;
	if (!args.empty())
		line += " " + args;
	return (line);
}

// ":<prefix> <command> <args>" for everything the server relays on a client's
// behalf — JOIN, PART, PRIVMSG, KICK, TOPIC, MODE, NICK, QUIT.
//
// `prefix` is the ORIGINATING client's nick!user@host, built from the Client
// object rather than from anything the sender put on the wire. That is what
// makes prefix spoofing impossible: parseMessage does read a client-supplied
// prefix, but nothing ever passes it here.
std::string	irc::fromClient(const std::string &prefix,
	const std::string &command, const std::string &args)
{
	std::string	line;

	line = ":" + prefix + " " + command;
	if (!args.empty())
		line += " " + args;
	return (line);
}
