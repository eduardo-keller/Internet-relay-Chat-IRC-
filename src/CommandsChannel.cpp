#include <string>

#include "Channel.hpp"
#include "Client.hpp"
#include "Command.hpp"
#include "Message.hpp"
#include "Replies.hpp"
#include "Server.hpp"

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
