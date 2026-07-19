#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <cstddef>
#include <set>
#include <string>

class Client;

// A channel and its mode state. Pure domain object: it answers questions and
// mutates its own state, it never sends anything. Handlers ask the Channel
// what is allowed, then use Server to deliver the result.
//
// OWNERSHIP: members and operators are non-owning Client* pointers into
// Server's client map. Channel never deletes a Client.
//
// The invite list holds Client*, NOT nicknames. Storing nicknames would mean
// an invite is silently lost the moment the invitee runs /nick: invite
// "alice", she becomes "bob", her JOIN looks up "bob" and finds nothing.
// Pointer identity survives a nick change for free.
//
// The cost is that invites must be cleared when a client disconnects.
// Server::reapDisconnected already sweeps every channel to drop the client
// from member and operator sets, so it clears the invite list in the same
// pass — including channels the client was invited to but never joined.
//
// The DOMAIN track owns the internals; only this shape is shared.
class Channel
{
	public:
		Channel();
		explicit Channel(const std::string &name);
		Channel(const Channel &other);
		Channel	&operator=(const Channel &other);
		~Channel();

		const std::string	&getName() const;
		const std::string	&getTopic() const;
		void				setTopic(const std::string &topic);

		// --- membership ---------------------------------------------------
		void	addMember(Client *client);
		void	removeMember(Client *client);
		bool	isMember(const Client *client) const;
		bool	isEmpty() const;
		std::size_t					memberCount() const;
		const std::set<Client *>	&getMembers() const;

		// --- operators ----------------------------------------------------
		void	addOperator(Client *client);
		void	removeOperator(Client *client);
		bool	isOperator(const Client *client) const;

		// --- invite list (mode +i) ----------------------------------------
		// Consumed on JOIN: a successful invited join removes the invite.
		void	addInvite(Client *client);
		void	removeInvite(Client *client);
		bool	isInvited(const Client *client) const;

		// --- modes i / t / k / l ------------------------------------------
		bool	isInviteOnly() const;
		void	setInviteOnly(bool value);
		bool	isTopicRestricted() const;
		void	setTopicRestricted(bool value);

		bool				hasKey() const;
		const std::string	&getKey() const;
		void				setKey(const std::string &key);
		void				clearKey();

		bool		hasUserLimit() const;
		std::size_t	getUserLimit() const;
		void		setUserLimit(std::size_t limit);
		void		clearUserLimit();

		// Renders current modes for RPL_CHANNELMODEIS (324), e.g. "+itk"
		// plus its parameters. Key is only disclosed to members.
		std::string	modeString(bool includeParams) const;

	private:
		std::string				_name;
		std::string				_topic;
		std::set<Client *>		_members;
		std::set<Client *>		_operators;
		std::set<Client *>		_invited;
		bool					_inviteOnly;
		bool					_topicRestricted;
		bool					_hasKey;
		std::string				_key;
		bool					_hasUserLimit;
		std::size_t				_userLimit;
};

#endif
