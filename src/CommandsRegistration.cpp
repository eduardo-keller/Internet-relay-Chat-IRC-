#include <string>

#include "Client.hpp"
#include "Command.hpp"
#include "Message.hpp"
#include "Replies.hpp"
#include "Server.hpp"
#include "Utils.hpp"

// The transport track's command handlers: the registration state machine.
//
// PASS -> NICK -> USER, and a client is registered once all three flags are
// set. The dispatcher has already decided that these commands are allowed to
// run at all (ARCHITECTURE.md section 4), so nothing here re-checks that to
// gain entry — the checks below are each command's own rules.

// Used by both 002 and 004. Decision D3 fixes the 004 payload as
// "ircserv 1.0 - itkol": no user modes at all, and the five channel modes we
// implement.
static const std::string	SERVER_VERSION = "1.0";

// The <target> of a numeric is the recipient's nickname, or "*" while they do
// not have one (ARCHITECTURE.md section 6). Deliberately a copy of the helper
// in Server.cpp rather than a shared export: it is four lines, and the domain
// handlers never need it — the registration gate means their sender always has
// a nickname by the time they run.
static std::string	replyTarget(const Client &client)
{
	if (client.getNickname().empty())
		return ("*");
	return (client.getNickname());
}

static void	replyNotRegistered(Server &server, Client &client)
{
	server.sendToClient(client, irc::numeric(server.getServerName(),
			irc::ERR_NOTREGISTERED, replyTarget(client),
			":You have not registered"));
}

static void	replyNeedMoreParams(Server &server, Client &client,
				const std::string &command)
{
	server.sendToClient(client, irc::numeric(server.getServerName(),
			irc::ERR_NEEDMOREPARAMS, replyTarget(client),
			command + " :Not enough parameters"));
}

static void	replyAlreadyRegistered(Server &server, Client &client)
{
	server.sendToClient(client, irc::numeric(server.getServerName(),
			irc::ERR_ALREADYREGISTRED, replyTarget(client),
			":Unauthorized command (already registered)"));
}

// 001 through 004, in that order, exactly once per connection.
//
// irssi treats 001 as "you are connected" and sits waiting forever without it.
static void	sendWelcomeBurst(Server &server, Client &client)
{
	const std::string	&name = server.getServerName();
	const std::string	&nick = client.getNickname();

	server.sendToClient(client, irc::numeric(name, irc::RPL_WELCOME, nick,
			":Welcome to the Internet Relay Network " + client.prefix()));
	server.sendToClient(client, irc::numeric(name, irc::RPL_YOURHOST, nick,
			":Your host is " + name + ", running version " + SERVER_VERSION));
	// __DATE__ and __TIME__ are filled in by the compiler, so 003 costs no
	// syscall and no stored state (decision D4).
	server.sendToClient(client, irc::numeric(name, irc::RPL_CREATED, nick,
			":This server was created "
			+ std::string(__DATE__) + " " + std::string(__TIME__)));
	server.sendToClient(client, irc::numeric(name, irc::RPL_MYINFO, nick,
			name + " " + SERVER_VERSION + " - itkol"));

	// THE FLAG IS WHAT MAKES THE BURST FIRE ONCE. isRegistered() stays true
	// forever after the transition, so without this separate memory every
	// later NICK would replay the welcome.
	client.setWelcomeSent(true);
}

// Called at the end of NICK and USER — the only two commands that can complete
// a registration.
//
// PASS deliberately does not call it. NICK and USER are refused before PASS,
// so PASS can never be the command that completes the set, and calling this
// there would be dead code dressed up as a safety net.
static void	completeRegistrationIfReady(Server &server, Client &client)
{
	if (client.isRegistered() && !client.welcomeSent())
		sendWelcomeBurst(server, client);
}

void	cmdPass(Server &server, Client &sender, const Message &msg)
{
	// PASS is a pre-registration command only. Afterwards it is 462 — note
	// the RFC's own spelling, ERR_ALREADYREGISTRED, missing an E.
	if (sender.isRegistered())
	{
		replyAlreadyRegistered(server, sender);
		return ;
	}
	if (msg.params.empty())
	{
		replyNeedMoreParams(server, sender, "PASS");
		return ;
	}
	if (msg.params[0] != server.getPassword())
	{
		// 464 AND THEN DISCONNECT. The numeric is queued first and the
		// disconnect only marks, so the reap flushes this line and the ERROR
		// after it before closing the socket: the client learns why it was
		// dropped instead of watching the connection vanish.
		server.sendToClient(sender, irc::numeric(server.getServerName(),
				irc::ERR_PASSWDMISMATCH, replyTarget(sender),
				":Password incorrect"));
		server.disconnectClient(sender, "Password incorrect");
		return ;
	}
	sender.setHasPass(true);
}

void	cmdNick(Server &server, Client &sender, const Message &msg)
{
	// AUTHORISATION BEFORE VALIDATION. RFC 2812 section 3.1.1: the password
	// must be set before any attempt to register, so a NICK with no PASS
	// behind it is refused outright rather than half-processed.
	//
	// This 451 comes from the HANDLER, not from the dispatcher's gate — NICK
	// is one of the six commands that gate lets through, precisely so that
	// this rule can live here where the reason for it is visible.
	if (!sender.hasPass())
	{
		replyNotRegistered(server, sender);
		return ;
	}
	if (msg.params.empty() || msg.params[0].empty())
	{
		server.sendToClient(sender, irc::numeric(server.getServerName(),
				irc::ERR_NONICKNAMEGIVEN, replyTarget(sender),
				":No nickname given"));
		return ;
	}

	const std::string	&nickname = msg.params[0];

	if (!utils::isValidNickname(nickname))
	{
		server.sendToClient(sender, irc::numeric(server.getServerName(),
				irc::ERR_ERRONEUSNICKNAME, replyTarget(sender),
				nickname + " :Erroneous nickname"));
		return ;
	}

	// COLLISIONS ARE CASE-INSENSITIVE: alice and ALICE are the same person,
	// and so are nick[42] and nick{42} (RFC 2812 section 2.2). That comparison
	// lives inside findClientByNick so no caller can forget it.
	Client	*holder = server.findClientByNick(nickname);

	if (holder != NULL && holder != &sender)
	{
		server.sendToClient(sender, irc::numeric(server.getServerName(),
				irc::ERR_NICKNAMEINUSE, replyTarget(sender),
				nickname + " :Nickname is already in use"));
		return ;
	}

	// A registered client changing nick has to tell everyone who can see it,
	// and the line carries the OLD prefix — that is how a client knows which
	// of its windows to rename. Built before the nickname is replaced.
	const bool			wasRegistered = sender.isRegistered();
	const std::string	changeLine = irc::fromClient(sender.prefix(), "NICK",
							":" + nickname);

	sender.setNickname(nickname);
	sender.setHasNick(true);
	if (wasRegistered)
	{
		// includeOrigin is true: irssi wants its own change echoed back, and
		// broadcastToPeers delivers to every peer exactly once however many
		// channels they share with the sender.
		server.broadcastToPeers(sender, changeLine, true);
		return ;
	}
	completeRegistrationIfReady(server, sender);
}

void	cmdUser(Server &server, Client &sender, const Message &msg)
{
	if (sender.isRegistered())
	{
		replyAlreadyRegistered(server, sender);
		return ;
	}
	if (!sender.hasPass())
	{
		replyNotRegistered(server, sender);
		return ;
	}

	// USER <username> <mode> <unused> :<realname> — four parameters, and the
	// middle two are ignored by design. The mode field is a 1993 addition
	// nobody sets, and the third is historically the client's own idea of the
	// server's name.
	if (msg.params.size() < 4)
	{
		replyNeedMoreParams(server, sender, "USER");
		return ;
	}
	sender.setUsername(msg.params[0]);
	sender.setRealname(msg.params[3]);
	sender.setHasUser(true);
	completeRegistrationIfReady(server, sender);
}

void	cmdQuit(Server &, Client &, const Message &)
{
}

void	cmdPing(Server &, Client &, const Message &)
{
}

void	cmdPong(Server &, Client &, const Message &)
{
}

void	cmdCap(Server &, Client &, const Message &)
{
}
