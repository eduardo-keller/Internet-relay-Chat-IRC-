#include "Channel.hpp"
#include "Client.hpp"

#include "harness.hpp"

// Unit tests for the DOMAIN track's Channel. The model is pure state: no
// socket, fd or Server is involved anywhere in this file.

static void	testIdentityAndTopic(void)
{
	Channel	defaulted;

	check(defaulted.getName().empty(),
		"channel identity: default name is empty");
	check(defaulted.getTopic().empty(),
		"channel topic: default topic is empty");

	Channel	channel("#chat");

	checkEqual(channel.getName(), "#chat",
		"channel identity: constructor stores the name");
	check(channel.getTopic().empty(),
		"channel topic: named channel starts without a topic");

	channel.setTopic("General discussion");
	checkEqual(channel.getTopic(), "General discussion",
		"channel topic: spaces are preserved");

	channel.setTopic("Project discussion");
	checkEqual(channel.getTopic(), "Project discussion",
		"channel topic: a new topic replaces the old one");

	channel.setTopic("");
	check(channel.getTopic().empty(),
		"channel topic: an empty topic clears it");
}

static void	testInitialState(void)
{
	Channel	channel("#chat");

	check(channel.isEmpty(),
		"channel initial state: it has no members");
	check(channel.memberCount() == 0,
		"channel initial state: member count is zero");
	check(!channel.isInviteOnly(),
		"channel initial state: invite-only mode is off");
	check(!channel.isTopicRestricted(),
		"channel initial state: topic restriction is off");
	check(!channel.hasKey(),
		"channel initial state: key mode is off");
	check(channel.getKey().empty(),
		"channel initial state: stored key is empty");
	check(!channel.hasUserLimit(),
		"channel initial state: user limit mode is off");
	check(channel.getUserLimit() == 0,
		"channel initial state: user limit value is zero");
}

static void	testMembers(void)
{
	Channel	channel("#chat");
	Client	alice(3, "localhost");
	Client	bob(4, "localhost");
	Client	outsider(5, "localhost");

	check(!channel.isMember(&alice),
		"channel members: a fresh client is not a member");

	channel.addMember(&alice);
	check(channel.isMember(&alice),
		"channel members: an added client becomes a member");
	check(channel.memberCount() == 1,
		"channel members: adding the first client updates the count");
	check(!channel.isEmpty(),
		"channel members: adding the first client makes it non-empty");

	channel.addMember(&alice);
	check(channel.memberCount() == 1,
		"channel members: adding the same client twice does not duplicate it");

	channel.addMember(&bob);
	check(channel.isMember(&alice) && channel.isMember(&bob),
		"channel members: two clients can coexist");
	check(channel.memberCount() == 2,
		"channel members: adding a second client updates the count");
	check(channel.getMembers().find(&alice) != channel.getMembers().end()
		&& channel.getMembers().find(&bob) != channel.getMembers().end(),
		"channel members: getMembers exposes both stored pointers");

	channel.removeMember(&alice);
	check(!channel.isMember(&alice) && channel.isMember(&bob),
		"channel members: removing one client keeps the other");
	check(channel.memberCount() == 1,
		"channel members: removing one client updates the count");

	channel.removeMember(&outsider);
	check(channel.memberCount() == 1 && channel.isMember(&bob),
		"channel members: removing a non-member is safe");

	channel.removeMember(&bob);
	check(channel.isEmpty() && channel.memberCount() == 0,
		"channel members: removing the last client makes it empty");

	channel.addMember(NULL);
	check(!channel.isMember(NULL) && channel.memberCount() == 0,
		"channel members: NULL is never stored as a member");
	channel.removeMember(NULL);
	check(channel.isEmpty(),
		"channel members: removing NULL is safe");
}

static void	testOperators(void)
{
	Channel	channel("#chat");
	Client	alice(3, "localhost");
	Client	bob(4, "localhost");
	Client	outsider(5, "localhost");

	channel.addMember(&alice);
	channel.addMember(&bob);
	check(!channel.isOperator(&alice),
		"channel operators: a member starts without operator privileges");

	channel.addOperator(&alice);
	check(channel.isOperator(&alice),
		"channel operators: a member can be promoted");

	channel.addOperator(&alice);
	check(channel.isOperator(&alice),
		"channel operators: adding the same operator twice is idempotent");

	channel.addOperator(&bob);
	check(channel.isOperator(&alice) && channel.isOperator(&bob),
		"channel operators: two operators can coexist");

	channel.removeOperator(&alice);
	check(!channel.isOperator(&alice) && channel.isOperator(&bob),
		"channel operators: removing one operator keeps the other");

	channel.removeOperator(&outsider);
	check(channel.isOperator(&bob),
		"channel operators: removing a non-operator is safe");

	channel.removeOperator(&bob);
	check(!channel.isOperator(&bob),
		"channel operators: an operator can be demoted");

	channel.addOperator(NULL);
	check(!channel.isOperator(NULL),
		"channel operators: NULL is never stored as an operator");
	channel.removeOperator(NULL);
	check(!channel.isOperator(NULL),
		"channel operators: removing NULL is safe");
}

void	runChannelTests(void)
{
	testIdentityAndTopic();
	testInitialState();
	testMembers();
	testOperators();
}
