#include "Channel.hpp"
#include "Client.hpp"

#include "harness.hpp"

#include <climits>

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

static void	testBooleanModes(void)
{
	Channel	channel("#chat");

	check(!channel.isInviteOnly() && !channel.isTopicRestricted(),
		"channel modes: i and t start disabled");

	channel.setInviteOnly(true);
	check(channel.isInviteOnly() && !channel.isTopicRestricted(),
		"channel modes: enabling i does not change t");
	channel.setInviteOnly(true);
	check(channel.isInviteOnly(),
		"channel modes: enabling i twice is idempotent");

	channel.setTopicRestricted(true);
	check(channel.isInviteOnly() && channel.isTopicRestricted(),
		"channel modes: enabling t does not change i");
	channel.setTopicRestricted(true);
	check(channel.isTopicRestricted(),
		"channel modes: enabling t twice is idempotent");

	channel.setInviteOnly(false);
	check(!channel.isInviteOnly() && channel.isTopicRestricted(),
		"channel modes: disabling i does not change t");
	channel.setInviteOnly(false);
	check(!channel.isInviteOnly(),
		"channel modes: disabling i twice is idempotent");

	channel.setTopicRestricted(false);
	check(!channel.isInviteOnly() && !channel.isTopicRestricted(),
		"channel modes: disabling t leaves both modes disabled");

	channel.setInviteOnly(true);
	channel.setTopicRestricted(true);
	Channel	copied(channel);

	check(copied.isInviteOnly() && copied.isTopicRestricted(),
		"channel modes: copy construction preserves i and t");
	copied.setInviteOnly(false);
	copied.setTopicRestricted(false);
	check(channel.isInviteOnly() && channel.isTopicRestricted(),
		"channel modes: changing copied modes does not change the original");

	Channel	assigned("#other");

	assigned = channel;
	check(assigned.isInviteOnly() && assigned.isTopicRestricted(),
		"channel modes: assignment preserves i and t");
}

static void	testKeyMode(void)
{
	Channel	channel("#chat");

	check(!channel.hasKey() && channel.getKey().empty(),
		"channel key: k starts disabled with no stored key");

	channel.setInviteOnly(true);
	channel.setTopicRestricted(true);
	channel.setKey("secret");
	check(channel.hasKey(),
		"channel key: setting a key enables k");
	checkEqual(channel.getKey(), "secret",
		"channel key: the configured key is preserved");
	check(channel.isInviteOnly() && channel.isTopicRestricted(),
		"channel key: setting k does not change i or t");

	channel.setKey("new-secret");
	check(channel.hasKey() && channel.getKey() == "new-secret",
		"channel key: setting a second key replaces the first");

	channel.clearKey();
	check(!channel.hasKey(),
		"channel key: clearing the key disables k");
	check(channel.getKey().empty(),
		"channel key: clearing k erases the stored value");
	check(channel.isInviteOnly() && channel.isTopicRestricted(),
		"channel key: clearing k does not change i or t");

	channel.clearKey();
	check(!channel.hasKey() && channel.getKey().empty(),
		"channel key: clearing k twice is idempotent");

	channel.setKey("copy-secret");
	Channel	copied(channel);

	check(copied.hasKey() && copied.getKey() == "copy-secret",
		"channel key: copy construction preserves k and its value");
	copied.clearKey();
	check(channel.hasKey() && channel.getKey() == "copy-secret",
		"channel key: clearing a copied key does not change the original");

	Channel	assigned("#other");

	assigned.setKey("old-secret");
	assigned = channel;
	check(assigned.hasKey() && assigned.getKey() == "copy-secret",
		"channel key: assignment replaces k and its value");
}

static void	testUserLimitMode(void)
{
	Channel	channel("#chat");

	check(!channel.hasUserLimit() && channel.getUserLimit() == 0,
		"channel limit: l starts disabled with a zero value");

	channel.setInviteOnly(true);
	channel.setTopicRestricted(true);
	channel.setKey("secret");
	channel.setUserLimit(10);
	check(channel.hasUserLimit(),
		"channel limit: setting a limit enables l");
	check(channel.getUserLimit() == 10,
		"channel limit: the configured value is preserved");
	check(channel.isInviteOnly() && channel.isTopicRestricted()
		&& channel.hasKey(),
		"channel limit: setting l does not change i, t or k");

	channel.setUserLimit(25);
	check(channel.hasUserLimit() && channel.getUserLimit() == 25,
		"channel limit: setting a second value replaces the first");

	channel.clearUserLimit();
	check(!channel.hasUserLimit(),
		"channel limit: clearing the limit disables l");
	check(channel.getUserLimit() == 0,
		"channel limit: clearing l resets the stored value to zero");
	check(channel.isInviteOnly() && channel.isTopicRestricted()
		&& channel.hasKey(),
		"channel limit: clearing l does not change i, t or k");

	channel.clearUserLimit();
	check(!channel.hasUserLimit() && channel.getUserLimit() == 0,
		"channel limit: clearing l twice is idempotent");

	channel.setUserLimit(42);
	Channel	copied(channel);

	check(copied.hasUserLimit() && copied.getUserLimit() == 42,
		"channel limit: copy construction preserves l and its value");
	copied.clearUserLimit();
	check(channel.hasUserLimit() && channel.getUserLimit() == 42,
		"channel limit: clearing a copied limit does not change the original");

	Channel	assigned("#other");

	assigned.setUserLimit(3);
	assigned = channel;
	check(assigned.hasUserLimit() && assigned.getUserLimit() == 42,
		"channel limit: assignment replaces l and its value");
}

static void	testModeString(void)
{
	Channel	channel("#chat");

	checkEqual(channel.modeString(true), "+",
		"channel mode string: no active mode renders plus");
	checkEqual(channel.modeString(false), "+",
		"channel mode string: hiding absent params still renders plus");

	channel.setInviteOnly(true);
	checkEqual(channel.modeString(true), "+i",
		"channel mode string: i renders without a parameter");
	channel.setTopicRestricted(true);
	checkEqual(channel.modeString(true), "+it",
		"channel mode string: i and t use stable order");
	channel.setInviteOnly(false);
	checkEqual(channel.modeString(true), "+t",
		"channel mode string: t can render on its own");
	channel.setTopicRestricted(false);

	channel.setKey("secret");
	checkEqual(channel.modeString(true), "+k secret",
		"channel mode string: k includes its key when requested");
	checkEqual(channel.modeString(false), "+k",
		"channel mode string: k hides its key when params are excluded");
	channel.clearKey();

	channel.setUserLimit(10);
	checkEqual(channel.modeString(true), "+l 10",
		"channel mode string: l includes its limit when requested");
	checkEqual(channel.modeString(false), "+l",
		"channel mode string: l hides its limit when params are excluded");
	channel.clearUserLimit();

	// Enable in reverse order to prove output order depends on mode names,
	// not on the order in which setters happened to be called.
	channel.setUserLimit(10);
	channel.setKey("secret");
	channel.setTopicRestricted(true);
	channel.setInviteOnly(true);
	checkEqual(channel.modeString(true), "+itkl secret 10",
		"channel mode string: all modes and params use canonical order");
	checkEqual(channel.modeString(false), "+itkl",
		"channel mode string: all params can be excluded");

	channel.clearKey();
	checkEqual(channel.modeString(true), "+itl 10",
		"channel mode string: clearing k removes its flag and old key");
	channel.setKey("secret");
	channel.clearUserLimit();
	checkEqual(channel.modeString(true), "+itk secret",
		"channel mode string: clearing l removes its flag and old limit");

	Channel	maxLimit("#max");

	maxLimit.setUserLimit(static_cast<std::size_t>(INT_MAX));
	checkEqual(maxLimit.modeString(true), "+l 2147483647",
		"channel mode string: INT_MAX converts without narrowing loss");
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
	original.addInvite(&outsider);
	original.setInviteOnly(true);
	original.setTopicRestricted(true);
	original.setKey("full-secret");
	original.setUserLimit(50);

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
	check(copied.isInvited(&outsider),
		"channel OCF: copy constructor preserves invitations");
	check(copied.modeString(true) == "+itkl full-secret 50",
		"channel OCF: copy constructor preserves all active modes");

	copied.setTopic("Copied topic");
	copied.removeMember(&bob);
	copied.removeOperator(&alice);
	copied.removeInvite(&outsider);
	copied.setInviteOnly(false);
	copied.setTopicRestricted(false);
	copied.clearKey();
	copied.clearUserLimit();
	checkEqual(original.getTopic(), "Original topic",
		"channel OCF: changing the copy does not change the original topic");
	check(original.memberCount() == 2 && original.isMember(&bob),
		"channel OCF: changing copied members does not change the original");
	check(original.isOperator(&alice),
		"channel OCF: changing copied operators does not change the original");
	check(original.isInvited(&outsider),
		"channel OCF: changing copied invites does not change the original");
	check(original.modeString(true) == "+itkl full-secret 50",
		"channel OCF: changing copied modes does not change the original");

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
	check(assigned.isInvited(&outsider),
		"channel OCF: assignment replaces the invitation set");
	check(assigned.modeString(true) == "+itkl full-secret 50",
		"channel OCF: assignment replaces all mode state");

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
	testBooleanModes();
	testKeyMode();
	testUserLimitMode();
	testModeString();
	testOrthodoxCanonicalForm();
}
