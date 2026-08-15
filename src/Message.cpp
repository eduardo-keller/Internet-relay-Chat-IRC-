#include "Message.hpp"

// Parses one raw line — already stripped of its CRLF by
// Client::extractCommand — into the shared Message struct. Pure: no fds, no
// sockets, no state. See include/Message.hpp and docs/ARCHITECTURE.md section 5.

// hasTrailing starts false. What it records is that a ':' marker was actually
// present — not that the last param is non-empty, since ":" on its own is a
// legal EMPTY trailing parameter.
//
// Being honest about its weight: params.size() already separates
// "PRIVMSG #chan :" (two params, the last one empty) from "PRIVMSG #chan" (one
// param). The flag's real job is to stand as a guarantee that an empty trailing
// is PUSHED as a param rather than quietly dropped — an implementation that
// skipped it would make those two lines indistinguishable.
Message::Message() :
	prefix(),
	command(),
	params(),
	hasTrailing(false)
{
}

static std::string::size_type	skipSpaces(const std::string &line,
	std::string::size_type i)
{
	while (i < line.size() && line[i] == ' ')
		++i;
	return (i);
}

// Grammar: [ ':' prefix SPACE ] command *( SPACE middle ) [ SPACE ':' trailing ]
//
// A manual index scan rather than std::istringstream, and not for speed. The
// trailing parameter must keep its interior spaces, so at the moment the marker
// is found the parser needs "the raw rest of the line from exactly here" — a
// positional operation. With >> the position is consumed and recovering it
// afterwards is awkward to do and worse to defend. With an index it is
// substr(i + 1).
//
// A malformed line yields an EMPTY command and the caller ignores it without
// replying (docs/ARCHITECTURE.md section 5). Silence rather than 421 matters
// because extractCommand legitimately produces empty lines — "\r\n" on its own
// is a complete, empty line, and clients do send them — so answering would be
// answering noise.
//
// The command keeps the case it arrived in. Dispatch is keyed by the uppercased
// name, but doing that here would destroy information a parser has no business
// discarding, and 421 <command> :Unknown command echoes the command back: a
// user who typed "privmsgx" should see "privmsgx".
Message	parseMessage(const std::string &line)
{
	Message					msg;
	std::string::size_type	i = 0;
	std::string::size_type	end;

	// Clients are not supposed to send a prefix; the server adds one when it
	// relays. Nothing here reads msg.prefix either, since handlers learn who
	// sent a message from their Client& argument. It is parsed because it must
	// at least be SKIPPED: leave it in place and ":foo PRIVMSG #chan :hi"
	// parses ":foo" as the command name and loses the real one.
	if (i < line.size() && line[i] == ':')
	{
		end = line.find(' ', i);
		if (end == std::string::npos)
			return (msg);
		msg.prefix = line.substr(i + 1, end - i - 1);
		i = end;
	}

	i = skipSpaces(line, i);
	end = line.find(' ', i);
	if (end == std::string::npos)
		end = line.size();
	msg.command = line.substr(i, end - i);

	i = skipSpaces(line, end);
	while (i < line.size())
	{
		// The marker is a ':' at the START of a parameter, which is what makes
		// "PRIVMSG #chan a:b" a single param: that colon is mid-token, so it
		// is ordinary text. Everything after a real marker is content —
		// spaces, and any further colons, included.
		if (line[i] == ':')
		{
			msg.params.push_back(line.substr(i + 1));
			msg.hasTrailing = true;
			return (msg);
		}
		end = line.find(' ', i);
		if (end == std::string::npos)
			end = line.size();
		msg.params.push_back(line.substr(i, end - i));
		i = skipSpaces(line, end);
	}
	return (msg);
}
