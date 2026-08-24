#include <set>
#include <string>
#include <vector>

#include "Channel.hpp"
#include "Client.hpp"
#include "Command.hpp"
#include "Limits.hpp"
#include "Message.hpp"
#include "Replies.hpp"
#include "Server.hpp"
#include "Utils.hpp"

// The channel command handlers: JOIN, PART, PRIVMSG, KICK, INVITE, TOPIC and
// MODE. See docs/FASE3.md for the order they are being built in.
//
// THE SENDER ALWAYS HAS A NICKNAME IN THIS FILE, and that is worth stating
// once rather than defending seven times. None of these commands is in
// Server::isAllowedBeforeRegistration, so the dispatcher's registration gate
// has already answered 451 to anyone who has not completed PASS/NICK/USER.
// Hence sender.getNickname() directly as the <target> of a numeric — the
// replyTarget helper that Server.cpp and CommandsRegistration.cpp both carry,
// with its "*" fallback, has nothing to do here.
//
// No handler in this file sees a file descriptor, calls send(), or normalises
// a channel name. The last one is decision D5: Server keys _channels by
// utils::toIrcLower while Channel::getName() keeps the original spelling, so a
// handler passes whatever the client typed and gets the right channel back.

// RPL_NAMREPLY — the member list a client gets on JOIN.
//
// IT IS SENT IN AS MANY LINES AS IT TAKES, and that is not gold plating. A 353
// is capped at 510 bytes like every other message, and Server::sendToClient
// truncates anything longer without asking: one line would silently drop
// members from a channel with more than a dozen people in it, and the client's
// nick list would be wrong with nothing to show for it. Real servers split, so
// this splits.
//
// Operators carry '@'. We implement no voice, so there is no '+' prefix here.
//
// The member set is a std::set<Client *>, so the ORDER IS BY ADDRESS and
// varies between runs (FASE2.md section 3.3, item 7). Nothing may depend on
// it, here or in a test.
static void	sendNamesReply(Server &server, Client &sender, Channel &channel)
{
	// Built once and reused as the head of every line. It already ends with
	// the ':' that opens the trailing parameter, so the budget below is simply
	// what is left of MAX_PAYLOAD_LEN after it.
	const std::string	head = irc::numeric(server.getServerName(),
							irc::RPL_NAMREPLY, sender.getNickname(),
							"= " + channel.getName() + " :");
	const std::set<Client *>	&members = channel.getMembers();
	std::string					names;

	for (std::set<Client *>::const_iterator it = members.begin();
		it != members.end(); ++it)
	{
		if (*it == NULL)
			continue ;

		const std::string	entry = (channel.isOperator(*it) ? "@" : "")
								+ (*it)->getNickname();

		// The +1 is the space that would join this entry to the previous one.
		// Flushing BEFORE appending is what keeps the line under the limit
		// rather than one entry over it.
		if (!names.empty()
			&& head.size() + names.size() + 1 + entry.size()
			> irc::MAX_PAYLOAD_LEN)
		{
			server.sendToClient(sender, head + names);
			names.clear();
		}
		if (!names.empty())
			names += " ";
		names += entry;
	}
	// The sender has just been added, so the channel is never empty here and
	// this always sends the one remaining line.
	if (!names.empty())
		server.sendToClient(sender, head + names);
}

// The mode gates, in a fixed order, for ONE channel. Returns false when the
// client must be kept out, having already been told why.
//
// THE ORDER IS +i, THEN +k, THEN +l, and it is a decision rather than an
// accident: a channel can have all three set, only one numeric may come back,
// and the one that comes back should be the most specific reason. Invite-only
// is a statement about *who* may enter, so it outranks a key the client could
// have supplied and a limit that will change on its own. It is also the order
// the deployed servers use.
//
// AN INVITE OPENS +i AND NOTHING ELSE. It is not a master key: an invited
// client still produces the channel key and still waits for a seat. Treating an
// invitation as a bypass for all three would make +k unenforceable, since any
// operator could hand out entry to a keyed channel without the key.
static bool	passesModeGates(Server &server, Client &sender, Channel &channel,
				const std::string &key)
{
	const std::string	&name = channel.getName();
	const std::string	&nick = sender.getNickname();

	if (channel.isInviteOnly() && !channel.isInvited(&sender))
	{
		server.sendToClient(sender, irc::numeric(server.getServerName(),
				irc::ERR_INVITEONLYCHAN, nick,
				name + " :Cannot join channel (+i)"));
		return (false);
	}
	if (channel.hasKey() && key != channel.getKey())
	{
		server.sendToClient(sender, irc::numeric(server.getServerName(),
				irc::ERR_BADCHANNELKEY, nick,
				name + " :Cannot join channel (+k)"));
		return (false);
	}
	// >= because the limit counts the members already in: a channel of two with
	// a limit of two is full, and the third would make three.
	if (channel.hasUserLimit() && channel.memberCount() >= channel.getUserLimit())
	{
		server.sendToClient(sender, irc::numeric(server.getServerName(),
				irc::ERR_CHANNELISFULL, nick,
				name + " :Cannot join channel (+l)"));
		return (false);
	}
	return (true);
}

// Everything that happens for one channel of a JOIN list.
static void	joinOneChannel(Server &server, Client &sender,
				const std::string &name, const std::string &key)
{
	// AN EMPTY NAME IS SILENCE, NOT AN ERROR — decision D18.
	//
	// irssi 1.4.5 sends a bare "JOIN :" on its own on every single connection,
	// before it has even finished CAP negotiation. It was captured on the wire
	// with a pristine profile, so it is the client's behaviour and not a
	// leftover setting. A 403 or a 461 here would put an error line in the
	// status window of everybody who connects, and the subject requires the
	// reference client to connect "without encountering any error".
	//
	// The same answer covers an empty FIELD inside a list — "JOIN #a,,#b". It
	// names no channel either, and split only keeps it so that the key list
	// stays aligned.
	//
	// Note what this does NOT do: JOIN with no parameter at all is still 461,
	// in cmdJoin below. That form is a typo at an nc prompt, not something any
	// client sends, and answering it is how the user learns what they got wrong.
	if (name.empty())
		return ;

	if (!utils::isValidChannelName(name))
	{
		server.sendToClient(sender, irc::numeric(server.getServerName(),
				irc::ERR_NOSUCHCHANNEL, sender.getNickname(),
				name + " :No such channel"));
		return ;
	}

	Channel	*channel = server.findChannel(name);

	// AN EXISTING CHANNEL IS GATED; A NEW ONE CANNOT BE. Looking it up rather
	// than creating it straight away is what keeps a refused JOIN from leaving
	// an empty channel behind — getOrCreateChannel would have made one before
	// passesModeGates ever ran, and nothing would clean it up.
	if (channel != NULL)
	{
		// Already in it: say nothing. Replaying the whole sequence would make
		// irssi redraw the channel and re-announce the arrival to everyone.
		if (channel->isMember(&sender))
			return ;
		if (!passesModeGates(server, sender, *channel, key))
			return ;
		channel->addMember(&sender);
		// THE INVITE IS SPENT ON THE WAY IN, and only on the way in. A single
		// use is what makes +i mean anything after the first visit; consuming
		// it on a REFUSED attempt would instead let a wrong key burn someone
		// else's invitation.
		channel->removeInvite(&sender);
	}
	else
	{
		// A channel that did not exist has no modes to check, and its creator
		// becomes its operator.
		channel = server.getOrCreateChannel(name);
		channel->addMember(&sender);
		channel->addOperator(&sender);
	}

	// The order below is the contract (ARCHITECTURE.md section 6), and getting
	// it wrong is the usual reason a channel window opens empty in irssi.

	// 1. The JOIN itself, to EVERY member INCLUDING the sender: their client
	//    uses this echo to open the window, and the others use it to update
	//    their nick list. Hence `except` = NULL.
	server.broadcastToChannel(*channel,
		irc::fromClient(sender.prefix(), "JOIN", channel->getName()), NULL);

	// 2. The topic, only to the joiner.
	if (channel->getTopic().empty())
		server.sendToClient(sender, irc::numeric(server.getServerName(),
				irc::RPL_NOTOPIC, sender.getNickname(),
				channel->getName() + " :No topic is set"));
	else
		server.sendToClient(sender, irc::numeric(server.getServerName(),
				irc::RPL_TOPIC, sender.getNickname(),
				channel->getName() + " :" + channel->getTopic()));

	// 3. and 4. The member list and its terminator, also only to the joiner.
	sendNamesReply(server, sender, *channel);
	server.sendToClient(sender, irc::numeric(server.getServerName(),
			irc::RPL_ENDOFNAMES, sender.getNickname(),
			channel->getName() + " :End of /NAMES list"));
}

// JOIN <channel>{,<channel>} [<key>{,<key>}]
//
// KEYS ARE POSITIONAL: the nth key belongs to the nth channel, and a channel
// with no key of its own is written as an empty field. That is precisely why
// utils::split preserves empty fields — "JOIN #a,#b ,key2" must give #a no key
// and #b the key. Collapse that empty field and the list becomes ["key2"],
// which then lands on #a: the user is refused entry to one channel and hands
// its key to another.
//
// Each channel is processed independently. One bad name does not cancel the
// rest: the client asked for several things, and failing all of them because
// one was wrong is worse service than the error itself.
void	cmdJoin(Server &server, Client &sender, const Message &msg)
{
	if (msg.params.empty())
	{
		server.sendToClient(sender, irc::numeric(server.getServerName(),
				irc::ERR_NEEDMOREPARAMS, sender.getNickname(),
				"JOIN :Not enough parameters"));
		return ;
	}

	const std::vector<std::string>	names = utils::split(msg.params[0], ',');
	std::vector<std::string>		keys;

	if (msg.params.size() > 1)
		keys = utils::split(msg.params[1], ',');

	for (std::vector<std::string>::size_type i = 0; i < names.size(); ++i)
	{
		// Fewer keys than channels is normal and legal: the channels past the
		// end of the key list simply have none.
		const std::string	key = (i < keys.size()) ? keys[i] : std::string();

		// THE DISCONNECT CHECK IS NOT DECORATION. sendToClient can overflow a
		// client's SendQ, which marks it for disconnection, and this loop may
		// still have five channels to go. Carrying on would keep queueing onto
		// a client that is being reaped at the end of this poll iteration.
		if (sender.isDisconnecting())
			return ;
		joinOneChannel(server, sender, names[i], key);
	}
}

// MODE, in the smallest form that keeps irssi's status window clean.
//
// Step 0.6 of docs/FASE3.md. It answers the QUERY and changes nothing; the
// mode string parser and the five flags arrive in steps 9 to 12.
//
// THE SILENT BRANCH IS THE REASON THIS EXISTS EARLY. irssi 1.4.5 sends
// "MODE <nick> +i" on its own, roughly two seconds after registering, on every
// connection and without joining anything — captured on the wire in step 0.
// It also sends "MODE <channel>" by itself after every JOIN. With no handler
// at all, the dispatcher answers 421 :Unknown command to both, and the subject
// requires the reference client to connect "without encountering any error".
//
// Answering 403 :No such channel to a user mode would be the same mistake in
// nicer clothes — which is exactly how decision D2 went wrong with CAP. We
// implement no user modes, so we have nothing to say about one. Measured: with
// no reply, the status window stays clean.
void	cmdMode(Server &server, Client &sender, const Message &msg)
{
	if (msg.params.empty())
	{
		server.sendToClient(sender, irc::numeric(server.getServerName(),
				irc::ERR_NEEDMOREPARAMS, sender.getNickname(),
				"MODE :Not enough parameters"));
		return ;
	}

	const std::string	&target = msg.params[0];

	// '#' is our only channel prefix (ARCHITECTURE.md section 5), so anything
	// else is a user mode, and a user mode is none of our business.
	if (target.empty() || target[0] != '#')
		return ;

	Channel	*channel = server.findChannel(target);

	if (channel == NULL)
	{
		// The name is echoed back AS THE CLIENT TYPED IT. There is no channel
		// to take a canonical spelling from, and quoting the request is what
		// makes the error legible.
		server.sendToClient(sender, irc::numeric(server.getServerName(),
				irc::ERR_NOSUCHCHANNEL, sender.getNickname(),
				target + " :No such channel"));
		return ;
	}

	// A mode STRING was supplied, so this is a change request. Steps 9 to 12
	// own that. Until then the answer is silence rather than a guess: 472
	// would claim the flag is unknown when we simply have not read it yet, and
	// 482 would announce a permission decision this step cannot make.
	if (msg.params.size() > 1)
		return ;

	// The query. isMember decides whether the parameters travel with the
	// flags, and the parameter that matters is the key: handing +k's key to a
	// stranger would make the mode pointless, since they could read it here
	// and walk straight in. getName() supplies the original spelling.
	server.sendToClient(sender, irc::numeric(server.getServerName(),
			irc::RPL_CHANNELMODEIS, sender.getNickname(),
			channel->getName() + " "
			+ channel->modeString(channel->isMember(&sender))));
}
