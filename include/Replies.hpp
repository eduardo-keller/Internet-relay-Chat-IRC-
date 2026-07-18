#ifndef REPLIES_HPP
#define REPLIES_HPP

#include <string>

// Shared reply vocabulary. Both tracks send numerics, so neither track may
// invent a code or a wording — see the numerics table in docs/ARCHITECTURE.md
// for the exact parameter text of every code listed here, taken from
// RFC 2812 section 5.
//
// These are namespace-scope const ints: internal linkage in C++98, so no ODR
// problem from including this header everywhere.
namespace irc
{
	// --- registration ---
	const int	RPL_WELCOME				= 1;
	const int	RPL_YOURHOST			= 2;
	const int	RPL_CREATED				= 3;
	const int	RPL_MYINFO				= 4;

	// --- channel state ---
	const int	RPL_CHANNELMODEIS		= 324;
	const int	RPL_NOTOPIC				= 331;
	const int	RPL_TOPIC				= 332;
	const int	RPL_INVITING			= 341;
	const int	RPL_NAMREPLY			= 353;
	const int	RPL_ENDOFNAMES			= 366;

	// --- errors ---
	const int	ERR_NOSUCHNICK			= 401;
	const int	ERR_NOSUCHCHANNEL		= 403;
	const int	ERR_CANNOTSENDTOCHAN	= 404;
	const int	ERR_NORECIPIENT			= 411;
	const int	ERR_NOTEXTTOSEND		= 412;
	const int	ERR_UNKNOWNCOMMAND		= 421;
	const int	ERR_NONICKNAMEGIVEN		= 431;
	const int	ERR_ERRONEUSNICKNAME	= 432;
	const int	ERR_NICKNAMEINUSE		= 433;
	const int	ERR_USERNOTINCHANNEL	= 441;
	const int	ERR_NOTONCHANNEL		= 442;
	const int	ERR_USERONCHANNEL		= 443;
	const int	ERR_NOTREGISTERED		= 451;
	const int	ERR_NEEDMOREPARAMS		= 461;
	const int	ERR_ALREADYREGISTRED	= 462;	// RFC spelling, sic
	const int	ERR_PASSWDMISMATCH		= 464;
	const int	ERR_CHANNELISFULL		= 471;
	const int	ERR_UNKNOWNMODE			= 472;
	const int	ERR_INVITEONLYCHAN		= 473;
	const int	ERR_BADCHANNELKEY		= 475;
	const int	ERR_CHANOPRIVSNEEDED	= 482;

	// Builds ":<serverName> <3-digit code> <target> <args>".
	// `target` is the recipient's nickname, or "*" before registration.
	// `args` is everything after the target, already including the ':' of a
	// trailing parameter. No CRLF: Server::sendToClient appends it.
	std::string	numeric(const std::string &serverName, int code,
					const std::string &target, const std::string &args);

	// Builds ":<prefix> <command> <args>" for message relays such as
	// JOIN / PART / PRIVMSG / KICK / MODE / QUIT echoes, where `prefix` is
	// the originating client's nick!user@host.
	std::string	fromClient(const std::string &prefix,
					const std::string &command, const std::string &args);
}

#endif
