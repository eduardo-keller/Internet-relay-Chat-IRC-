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

static void	testInvites(void)
{
	Channel	channel("#chat");
	Client	alice(3, "localhost");
	Client	bob(4, "localhost");
	Client	outsider(5, "localhost");

	bob.setNickname("bob");
	check(!channel.isInvited(&bob),
		"channel invites: a fresh client is not invited");

	channel.addInvite(&bob);
	check(channel.isInvited(&bob),
		"channel invites: an invited client is recorded");

	channel.addInvite(&bob);
	channel.removeInvite(&bob);
	check(!channel.isInvited(&bob),
		"channel invites: adding twice still needs only one removal");

	channel.addInvite(&alice);
	channel.addInvite(&bob);
	check(channel.isInvited(&alice) && channel.isInvited(&bob),
		"channel invites: two invitations can coexist");

	bob.setNickname("renamed-bob");
	check(channel.isInvited(&bob),
		"channel invites: an invitation survives a nickname change");

	channel.removeInvite(&bob);
	check(!channel.isInvited(&bob) && channel.isInvited(&alice),
		"channel invites: removing one invitation keeps the other");

	channel.removeInvite(&outsider);
	check(channel.isInvited(&alice),
		"channel invites: removing a missing invitation is safe");

	Channel	copied(channel);

	check(copied.isInvited(&alice),
		"channel invites: copy construction preserves invitations");
	copied.removeInvite(&alice);
	check(channel.isInvited(&alice),
		"channel invites: copied invitation set is independent");

	Channel	assigned("#other");

	assigned.addInvite(&outsider);
	assigned = channel;
	check(assigned.isInvited(&alice) && !assigned.isInvited(&outsider),
		"channel invites: assignment replaces the invitation set");

	channel.removeInvite(&alice);
	check(!channel.isInvited(&alice),
		"channel invites: a successful removal consumes the invitation");

	channel.addInvite(NULL);
	check(!channel.isInvited(NULL),
		"channel invites: NULL is never stored as invited");
	channel.removeInvite(NULL);
	check(!channel.isInvited(NULL),
		"channel invites: removing NULL is safe");
}

static void	testOrthodoxCanonicalForm(void)
{
	Client	alice(3, "localhost");
	Client	bob(4, "localhost");
	Client	outsider(5, "localhost");
	Channel	original("#original");

	original.setTopic("Original topic");
	original.addMember(&alice);
	original.addMember(&bob);
	original.addOperator(&alice);

	Channel	copied(original);

	checkEqual(copied.getName(), "#original",
		"channel OCF: copy constructor preserves the name");
	checkEqual(copied.getTopic(), "Original topic",
		"channel OCF: copy constructor preserves the topic");
	check(copied.memberCount() == 2 && copied.isMember(&alice)
		&& copied.isMember(&bob),
		"channel OCF: copy constructor preserves members");
	check(copied.isOperator(&alice),
		"channel OCF: copy constructor preserves operators");
	check(!copied.isInviteOnly() && !copied.isTopicRestricted()
		&& !copied.hasKey() && !copied.hasUserLimit(),
		"channel OCF: copy constructor preserves initial mode state");

	copied.setTopic("Copied topic");
	copied.removeMember(&bob);
	copied.removeOperator(&alice);
	checkEqual(original.getTopic(), "Original topic",
		"channel OCF: changing the copy does not change the original topic");
	check(original.memberCount() == 2 && original.isMember(&bob),
		"channel OCF: changing copied members does not change the original");
	check(original.isOperator(&alice),
		"channel OCF: changing copied operators does not change the original");

	Channel	assigned("#old");

	assigned.setTopic("Old topic");
	assigned.addMember(&outsider);
	assigned.addOperator(&outsider);
	assigned = original;
	checkEqual(assigned.getName(), "#original",
		"channel OCF: assignment replaces the name");
	checkEqual(assigned.getTopic(), "Original topic",
		"channel OCF: assignment replaces the topic");
	check(assigned.memberCount() == 2 && assigned.isMember(&alice)
		&& assigned.isMember(&bob) && !assigned.isMember(&outsider),
		"channel OCF: assignment replaces the member set");
	check(assigned.isOperator(&alice) && !assigned.isOperator(&outsider),
		"channel OCF: assignment replaces the operator set");

	assigned.removeMember(&alice);
	check(original.isMember(&alice),
		"channel OCF: assigned member set is independent from the original");

	Channel	*alias = &assigned;

	assigned = *alias;
	check(assigned.getName() == "#original" && assigned.isMember(&bob)
		&& assigned.isOperator(&alice),
		"channel OCF: self-assignment leaves the object intact");

	{
		Channel	temporary(original);

		check(temporary.isMember(&alice),
			"channel OCF: a temporary copy sees the original client pointer");
	}
	alice.setNickname("still-alive");
	checkEqual(alice.getNickname(), "still-alive",
		"channel OCF: destroying a copy does not destroy its clients");
}

void	runChannelTests(void)
{
	testIdentityAndTopic();
	testInitialState();
	testMembers();
	testOperators();
	testInvites();
	testOrthodoxCanonicalForm();
}
