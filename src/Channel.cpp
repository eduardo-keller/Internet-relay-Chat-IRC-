#include "Channel.hpp"

// Pure domain state. Channel does not own the Client pointers held by its
// sets, and no network operation belongs in this class.

Channel::Channel() :
	_name(),
	_topic(),
	_members(),
	_operators(),
	_invited(),
	_inviteOnly(false),
	_topicRestricted(false),
	_hasKey(false),
	_key(),
	_hasUserLimit(false),
	_userLimit(0)
{
}

Channel::Channel(const std::string &name) :
	_name(name),
	_topic(),
	_members(),
	_operators(),
	_invited(),
	_inviteOnly(false),
	_topicRestricted(false),
	_hasKey(false),
	_key(),
	_hasUserLimit(false),
	_userLimit(0)
{
}

// The sets themselves are copied, so later insertions and removals in one
// Channel do not affect the other. Their Client* values still point to the
// same Server-owned clients: copying a Channel never clones a Client.
Channel::Channel(const Channel &other) :
	_name(other._name),
	_topic(other._topic),
	_members(other._members),
	_operators(other._operators),
	_invited(other._invited),
	_inviteOnly(other._inviteOnly),
	_topicRestricted(other._topicRestricted),
	_hasKey(other._hasKey),
	_key(other._key),
	_hasUserLimit(other._hasUserLimit),
	_userLimit(other._userLimit)
{
}

// Same ownership rule as the copy constructor. Assignment replaces each set
// with a separate copy of the container while preserving pointer identity.
Channel	&Channel::operator=(const Channel &other)
{
	if (this != &other)
	{
		_name = other._name;
		_topic = other._topic;
		_members = other._members;
		_operators = other._operators;
		_invited = other._invited;
		_inviteOnly = other._inviteOnly;
		_topicRestricted = other._topicRestricted;
		_hasKey = other._hasKey;
		_key = other._key;
		_hasUserLimit = other._hasUserLimit;
		_userLimit = other._userLimit;
	}
	return (*this);
}

// Nothing is deleted here. Server owns every Client; these sets only hold
// non-owning pointers and release their pointer values automatically.
Channel::~Channel()
{
}

const std::string	&Channel::getName() const
{
	return (_name);
}

const std::string	&Channel::getTopic() const
{
	return (_topic);
}

void	Channel::setTopic(const std::string &topic)
{
	_topic = topic;
}

// Membership is based on Client pointer identity, not nickname. std::set
// makes repeated additions idempotent, and rejecting NULL keeps later
// broadcasts from ever encountering an invalid member entry.
void	Channel::addMember(Client *client)
{
	if (client != NULL)
		_members.insert(client);
}

void	Channel::removeMember(Client *client)
{
	if (client != NULL)
		_members.erase(client);
}

bool	Channel::isMember(const Client *client) const
{
	if (client == NULL)
		return (false);
	// The public query promises not to mutate the Client, while the set's key
	// type is Client*. Removing const here is only for lookup; the pointer is
	// never dereferenced or modified.
	return (_members.find(const_cast<Client *>(client)) != _members.end());
}

bool	Channel::isEmpty() const
{
	return (_members.empty());
}

std::size_t	Channel::memberCount() const
{
	return (_members.size());
}

const std::set<Client *>	&Channel::getMembers() const
{
	return (_members);
}

// Operator state is deliberately separate from membership. JOIN, MODE, PART
// and KICK handlers decide when to call both APIs; Channel only stores the
// state it is given. std::set prevents duplicate promotions.
void	Channel::addOperator(Client *client)
{
	if (client != NULL)
		_operators.insert(client);
}

void	Channel::removeOperator(Client *client)
{
	if (client != NULL)
		_operators.erase(client);
}

bool	Channel::isOperator(const Client *client) const
{
	if (client == NULL)
		return (false);
	// As in isMember, const is removed only to match the set's key type.
	return (_operators.find(const_cast<Client *>(client)) != _operators.end());
}

// Invites store Client* rather than nicknames, so an invitation survives a
// /nick change. A successful future cmdJoin consumes it explicitly through
// removeInvite; merely adding a member does not silently change this set.
void	Channel::addInvite(Client *client)
{
	if (client != NULL)
		_invited.insert(client);
}

void	Channel::removeInvite(Client *client)
{
	if (client != NULL)
		_invited.erase(client);
}

bool	Channel::isInvited(const Client *client) const
{
	if (client == NULL)
		return (false);
	// As in isMember, const is removed only to match the set's key type.
	return (_invited.find(const_cast<Client *>(client)) != _invited.end());
}

bool	Channel::isInviteOnly() const
{
	return (_inviteOnly);
}

// Channel stores mode state only. JOIN and TOPIC handlers interpret these
// flags and decide which numeric reply or mutation is allowed.
void	Channel::setInviteOnly(bool value)
{
	_inviteOnly = value;
}

bool	Channel::isTopicRestricted() const
{
	return (_topicRestricted);
}

void	Channel::setTopicRestricted(bool value)
{
	_topicRestricted = value;
}

bool	Channel::hasKey() const
{
	return (_hasKey);
}

const std::string	&Channel::getKey() const
{
	return (_key);
}

// Parameter validation belongs to cmdMode. Channel only records that a key
// is active and clears both pieces of state when mode k is removed.
void	Channel::setKey(const std::string &key)
{
	_key = key;
	_hasKey = true;
}

void	Channel::clearKey()
{
	_hasKey = false;
	_key.clear();
}

bool	Channel::hasUserLimit() const
{
	return (_hasUserLimit);
}

std::size_t	Channel::getUserLimit() const
{
	return (_userLimit);
}
