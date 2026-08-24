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

// --- session commands -----------------------------------------------------
//
// None of these four is in the subject's command list. They are here because
// the subject requires the reference client to connect "without encountering
// any error", and this is what that costs in practice: irssi opens with CAP,
// pings periodically and expects a matching PONG, and says QUIT on /quit.

void	cmdQuit(Server &server, Client &sender, const Message &msg)
{
	const std::string	reason = msg.params.empty() ? "Client quit"
							: msg.params[0];

	// MARKS, NEVER DELETES — and this is the handler that makes that matter.
	// One TCP packet can carry "QUIT :bye\r\nPRIVMSG #c :x\r\n"; the dispatch
	// loop that called us is still holding this Client& and still has lines
	// buffered. Deleting here would hand it a dangling reference. Instead the
	// client is flagged, the drain loop sees isDisconnecting() and stops
	// extracting, and reapDisconnected does the delete at the end of the poll
	// iteration — after flushing the ERROR this queues.
	//
	// disconnectClient also broadcasts the QUIT to everyone sharing a channel
	// with the sender, each of them exactly once.
	server.disconnectClient(sender, reason);
}

void	cmdPing(Server &server, Client &sender, const Message &msg)
{
	// irssi pings on a timer and reports a timeout if the token does not come
	// back. Echoing the token is the entire contract.
	//
	// RFC 2812 answers a token-less PING with 409 ERR_NOORIGIN, which is not in
	// the numeric table both tracks agreed on (ARCHITECTURE.md section 6).
	// Rather than invent a code, this uses 461: a missing token IS a missing
	// parameter, and the client learns the same thing.
	if (msg.params.empty())
	{
		replyNeedMoreParams(server, sender, "PING");
		return ;
	}
	server.sendToClient(sender, ":" + server.getServerName() + " PONG "
		+ server.getServerName() + " :" + msg.params[0]);
}

// ACCEPTED AND IGNORED, both of them, and this is the CAP lesson arriving a
// second time.
//
// irssi 1.4.5 sends "WHO <channel>" and "WHOIS <nick>" without being asked, as
// soon as there is a second person in a channel — captured on the wire in
// phase 3 step 1. Neither is in the subject's command list, and neither has
// anything to answer from here: this server keeps no user database, no idle
// times and no away state.
//
// So the choice is between 421 :Unknown command and nothing, and 421 puts an
// error line in the status window of BOTH users in an ordinary two-person
// session — which is the scenario the evaluation runs. The client displays
// nothing either way, so silence costs the user nothing and costs us no
// invented numerics.
//
// WHAT THIS PAIR REALLY DOCUMENTS is a measuring mistake. Step 0 captured a
// single irssi against an empty channel, concluded that neither command was
// ever sent, and wrote that into decision D15. Two clients in one channel
// disproved it immediately. One client is not a sample of two.
void	cmdWho(Server &, Client &, const Message &)
{
}

void	cmdWhois(Server &, Client &, const Message &)
{
}

void	cmdPong(Server &, Client &, const Message &)
{
	// Accepted and ignored, deliberately. A PONG is a client answering a PING
	// we sent, and this server sends none — it has no ping timer, so there is
	// no liveness state for a PONG to update. Answering it would be noise.
}

// Plain ASCII uppercasing, for the CAP subcommand.
//
// A deliberate copy of the helper in Server.cpp rather than a shared export,
// for the same reason replyTarget above is one: it is six lines, and exporting
// it would put a second general-purpose string utility next to utils::* that
// nobody else asked for. As there, utils::toIrcLower is NOT the right tool —
// its {}|^ rule is about nicknames and channel names, and a CAP subcommand is
// neither.
static std::string	toUpperAscii(const std::string &s)
{
	std::string	out(s);

	for (std::string::size_type i = 0; i < out.size(); ++i)
	{
		unsigned char	c = static_cast<unsigned char>(out[i]);

		if (c >= 'a' && c <= 'z')
			out[i] = static_cast<char>(c - 'a' + 'A');
	}
	return (out);
}

// IRCv3 capability negotiation, in the smallest shape that lets a real client
// through the door.
//
// DECISION D17 (docs/FASE3.md), WHICH SUPERSEDES D2. D2 made this handler a
// silent no-op: irssi sends "CAP LS 302" before PASS on every connect, we
// implement no capabilities, and answering 421 would print an error in its
// status window — which the subject forbids. The reasoning was right and the
// remedy was wrong. Pointed at irssi 1.4.5 for the first time, the silent
// version made it print "Waiting for CAP LS response..." and stop: it never
// sent PASS, NICK or USER, so NOBODY COULD REGISTER AT ALL. Silence is not a
// non-answer to this client; it is a stall.
//
// An empty list is the honest reply — "I speak no capabilities" — and irssi
// answers it with CAP END and gets on with registering. CAP stays outside the
// registration gate (Server::isAllowedBeforeRegistration), because the whole
// exchange happens before PASS.
void	cmdCap(Server &server, Client &sender, const Message &msg)
{
	// A bare "CAP" is legal input from nc and would index an empty vector
	// below. Nothing to negotiate, nothing to say.
	if (msg.params.empty())
		return ;

	const std::string	sub = toUpperAscii(msg.params[0]);
	const std::string	&name = server.getServerName();

	// The target is the sender's nickname, or "*" when they have none — which
	// is the usual case here, since CAP comes before NICK.
	if (sub == "LS")
	{
		// The trailing ':' with nothing after it IS the empty list. Dropping
		// the colon would make the line parse as having no trailing parameter
		// at all, which is a different message.
		server.sendToClient(sender, ":" + name + " CAP "
			+ replyTarget(sender) + " LS :");
	}
	else if (sub == "REQ")
	{
		// Nothing was offered, so nothing can be granted. The request is
		// echoed back inside the NAK so the client knows which one was
		// refused; a client that never saw a capability in our LS has no
		// business asking, but ACKing something we do not implement would be
		// a lie it might then rely on.
		const std::string	requested = msg.params.size() > 1
									? msg.params[1] : "";

		server.sendToClient(sender, ":" + name + " CAP "
			+ replyTarget(sender) + " NAK :" + requested);
	}
	// END, LIST, and anything else: silence. END closes the negotiation and
	// wants no reply — answering it would put a stray line in front of the
	// welcome burst.
}
