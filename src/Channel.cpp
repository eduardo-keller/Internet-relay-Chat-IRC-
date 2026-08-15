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

bool	Channel::isInviteOnly() const
{
	return (_inviteOnly);
}

bool	Channel::isTopicRestricted() const
{
	return (_topicRestricted);
}

bool	Channel::hasKey() const
{
	return (_hasKey);
}

const std::string	&Channel::getKey() const
{
	return (_key);
}

bool	Channel::hasUserLimit() const
{
	return (_hasUserLimit);
}

std::size_t	Channel::getUserLimit() const
{
	return (_userLimit);
}
