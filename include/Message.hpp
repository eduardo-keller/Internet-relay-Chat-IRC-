#ifndef MESSAGE_HPP
#define MESSAGE_HPP

#include <string>
#include <vector>

// Parser output. SHARED CONTRACT: the parser produces it, handlers consume it.
//
// One raw line (already stripped of its trailing CRLF) becomes one Message.
// Grammar (RFC 1459 / 2812):
//     [ ':' prefix SPACE ] command *( SPACE middle ) [ SPACE ':' trailing ]
//
// TRAILING RULE: the trailing parameter starts at the first " :" and runs to
// the end of the line, spaces included. It is stored as the LAST element of
// `params` with the leading ':' REMOVED. `hasTrailing` records that it was
// present, which matters because ":" alone is a valid empty trailing param.
//
// Example: ":nick!u@h PRIVMSG #chan :hello world"
//     prefix      = "nick!u@h"
//     command     = "PRIVMSG"
//     params      = ["#chan", "hello world"]
//     hasTrailing = true
struct Message
{
	std::string					prefix;
	std::string					command;
	std::vector<std::string>	params;
	bool						hasTrailing;

	Message();
};

// Pure function: no fds, no sockets. Unit-testable with constructed strings.
// A malformed line yields a Message with an empty `command`; callers must
// treat that as "ignore this line" rather than as an error reply.
Message	parseMessage(const std::string &line);

#endif
