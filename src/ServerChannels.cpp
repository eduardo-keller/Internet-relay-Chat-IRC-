#include <cstddef>
#include <map>
#include <set>
#include <string>

#include "Channel.hpp"
#include "Client.hpp"
#include "Server.hpp"
#include "Utils.hpp"

// Everything in Server that touches a Channel, in one translation unit.
//
// It replaces src/ServerChannels.stub.cpp, deleted in the same commit. The
// stub existed because the Makefile links every object into both binaries, so
// a single call to a Channel method with no Channel.cpp behind it stopped the
// whole build — including make test. See docs/FASE2.md section 3.
//
// THE CASEMAPPING CONTRACT (decision D5, ARCHITECTURE.md section 5) lives
// here, and it is the reason these three lookups exist at all: _channels is
// keyed by utils::toIrcLower(name), while Channel::getName() keeps the
// original spelling for display. A handler therefore passes whatever the
// client typed and gets the right channel back, and never normalises a name
// itself.
//
// Getting that wrong fails silently rather than loudly: without the
// normalisation, JOIN #Dev followed by PRIVMSG #dev creates TWO channels with
// one member each, and both users sit in a room that looks empty.

Channel	*Server::findChannel(const std::string &name)
{
	std::map<std::string, Channel *>::iterator	it
		= _channels.find(utils::toIrcLower(name));

	if (it == _channels.end())
		return (NULL);
	return (it->second);
}

Channel	*Server::getOrCreateChannel(const std::string &name)
{
	const std::string							key = utils::toIrcLower(name);
	std::map<std::string, Channel *>::iterator	it = _channels.find(key);

	if (it != _channels.end())
		return (it->second);

	// The KEY is lowercased; the NAME handed to the Channel is not. That
	// asymmetry is the whole design: lookups ignore case, display does not.
	Channel	*channel = new Channel(name);

	_channels[key] = channel;
	return (channel);
}

void	Server::removeChannel(const std::string &name)
{
	std::map<std::string, Channel *>::iterator	it
		= _channels.find(utils::toIrcLower(name));

	if (it == _channels.end())
		return ;
	delete it->second;
	_channels.erase(it);
}

// --- delivery -------------------------------------------------------------

// `except` is compared, never written to — which is why it can be a
// const Client*. It carries the sender of a PRIVMSG, who must not receive
// their own message back.
void	Server::broadcastToChannel(Channel &channel, const std::string &line,
			const Client *except)
{
	const std::set<Client *>	&members = channel.getMembers();

	// ITERATING WHILE SENDING IS SAFE, and only because nothing is deleted
	// inside the loop. sendToClient can fail on a full queue and mark a
	// recipient for disconnection, but marking does not touch this set:
	// removal happens in sweepChannels, from reapDisconnected, at the end of
	// the poll iteration. A version that deleted on the spot would invalidate
	// this iterator from under itself. See docs/FASE2.md section 3.3.
	for (std::set<Client *>::const_iterator it = members.begin();
		it != members.end(); ++it)
	{
		if (*it == NULL || *it == except)
			continue ;
		sendToClient(**it, line);
	}
}

// The interesting one. NICK and QUIT have to reach everybody who shares AT
// LEAST ONE channel with the origin, and each of them EXACTLY ONCE.
//
// Looping broadcastToChannel over the origin's channels is the obvious
// implementation and it is wrong: someone present in two of those channels
// gets the message twice. Collecting the recipients into a std::set first
// deduplicates by pointer identity, which is exactly the right notion of
// "the same person" here.
void	Server::broadcastToPeers(Client &origin, const std::string &line,
			bool includeOrigin)
{
	std::set<Client *>	recipients;

	for (std::map<std::string, Channel *>::iterator it = _channels.begin();
		it != _channels.end(); ++it)
	{
		Channel	*channel = it->second;

		if (!channel->isMember(&origin))
			continue ;

		const std::set<Client *>	&members = channel->getMembers();

		recipients.insert(members.begin(), members.end());
	}

	// includeOrigin is true for NICK, and a client that has joined nothing at
	// all still has to see its own nick change — so the origin is added
	// unconditionally rather than only when some channel already yielded it.
	// Inserting into a set is idempotent, so this is a no-op otherwise.
	if (includeOrigin)
		recipients.insert(&origin);

	for (std::set<Client *>::const_iterator it = recipients.begin();
		it != recipients.end(); ++it)
	{
		if (!includeOrigin && *it == &origin)
			continue ;
		sendToClient(**it, line);
	}
}

// --- disconnect sweep -----------------------------------------------------

// Removes a client from every membership, operator and invite set in the
// server, and drops any channel left empty.
//
// IT VISITS ALL CHANNELS, not just the ones the client joined. The invite list
// holds Client* rather than nicknames — a nickname would lose the invite the
// moment the invitee ran /nick — so a client can be referenced by a channel it
// never entered. Skipping those leaves a dangling pointer that the next
// INVITE-only JOIN dereferences.
//
// Channel holds NON-OWNING Client*, so this must run before the Client is
// deleted. reapDisconnected calls it in that order and nowhere else.
void	Server::sweepChannels(Client &client)
{
	std::map<std::string, Channel *>::iterator	it = _channels.begin();

	while (it != _channels.end())
	{
		Channel	*channel = it->second;

		channel->removeMember(&client);
		channel->removeOperator(&client);
		channel->removeInvite(&client);

		// A channel whose last member just left stops existing, along with
		// its modes, key and topic — which is what makes rejoining it a
		// fresh channel whose creator is the operator again.
		if (channel->isEmpty())
		{
			std::map<std::string, Channel *>::iterator	empty = it;

			++it;
			delete empty->second;
			_channels.erase(empty);
		}
		else
			++it;
	}
}

void	Server::clearAllChannels()
{
	for (std::map<std::string, Channel *>::iterator it = _channels.begin();
		it != _channels.end(); ++it)
		delete it->second;
	_channels.clear();
}
