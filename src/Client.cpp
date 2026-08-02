#include "Client.hpp"

// One connected user. See include/Client.hpp for the contract and
// docs/ARCHITECTURE.md sections 3 and 11 for the reasoning.
//
// Nothing in this file opens, reads, writes or closes a socket. The read and
// write buffers are plain strings; recv() and send() live in Server. That is
// what makes every function here testable with a hand-built string.

// "No socket attached". A default-constructed Client exists only because the
// Orthodox Canonical Form asks for it — Server always uses the (fd, hostname)
// constructor — so an _fd of -1 should never reach the poll loop.
static const int	NO_FD = -1;

Client::Client() :
	_fd(NO_FD),
	_nickname(),
	_username(),
	_realname(),
	_hostname(),
	_hasPass(false),
	_hasNick(false),
	_hasUser(false),
	_welcomeSent(false),
	_disconnecting(false),
	_quitReason(),
	_readBuffer(),
	_outputBuffer()
{
}

Client::Client(int fd, const std::string &hostname) :
	_fd(fd),
	_nickname(),
	_username(),
	_realname(),
	_hostname(hostname),
	_hasPass(false),
	_hasNick(false),
	_hasUser(false),
	_welcomeSent(false),
	_disconnecting(false),
	_quitReason(),
	_readBuffer(),
	_outputBuffer()
{
}

Client::Client(const Client &other) :
	_fd(other._fd),
	_nickname(other._nickname),
	_username(other._username),
	_realname(other._realname),
	_hostname(other._hostname),
	_hasPass(other._hasPass),
	_hasNick(other._hasNick),
	_hasUser(other._hasUser),
	_welcomeSent(other._welcomeSent),
	_disconnecting(other._disconnecting),
	_quitReason(other._quitReason),
	_readBuffer(other._readBuffer),
	_outputBuffer(other._outputBuffer)
{
}

Client	&Client::operator=(const Client &other)
{
	if (this != &other)
	{
		_fd = other._fd;
		_nickname = other._nickname;
		_username = other._username;
		_realname = other._realname;
		_hostname = other._hostname;
		_hasPass = other._hasPass;
		_hasNick = other._hasNick;
		_hasUser = other._hasUser;
		_welcomeSent = other._welcomeSent;
		_disconnecting = other._disconnecting;
		_quitReason = other._quitReason;
		_readBuffer = other._readBuffer;
		_outputBuffer = other._outputBuffer;
	}
	return (*this);
}

// Deliberately does NOT close(_fd).
//
// A destructor that closed the fd would make the copy constructor above a
// double-close bug: two Clients holding the same descriptor number, the second
// destructor closing an fd the kernel may already have handed to a new
// connection. Ownership of the descriptor belongs to Server, which closes it in
// reapDisconnected — the single place a Client is ever deleted.
Client::~Client()
{
}

// --- identity ------------------------------------------------------------
//
// The getters hand back a const reference: the caller only reads, and a
// reference costs no copy. The member outlives the call, so the reference is
// always valid.
//
// There is no setHostname on purpose. The hostname comes from accept() when
// the connection is created and never changes for the life of the client, so
// it is a constructor argument, not a mutable field.

const std::string	&Client::getNickname() const
{
	return (_nickname);
}

const std::string	&Client::getUsername() const
{
	return (_username);
}

const std::string	&Client::getRealname() const
{
	return (_realname);
}

const std::string	&Client::getHostname() const
{
	return (_hostname);
}

void	Client::setNickname(const std::string &nickname)
{
	_nickname = nickname;
}

void	Client::setUsername(const std::string &username)
{
	_username = username;
}
//necessary?
void	Client::setRealname(const std::string &realname)
{
	_realname = realname;
}

// "nick!user@host" — the source prefix of every message this client
// originates, i.e. what the other members of a channel see in front of a JOIN,
// PART, PRIVMSG, KICK, TOPIC, MODE, NICK or QUIT relayed from here.
// See docs/ARCHITECTURE.md section 6.
//
// Returned BY VALUE, unlike the getters above, because no member holds this
// string: it is assembled here. Returning a reference would hand back a
// dangling pointer into a temporary destroyed at the end of this call.
//
// Built on demand rather than cached, because NICK changes it. A cached copy
// would keep relaying the old nickname to everyone in the channel, and the
// bug would only show up after someone runs /nick.
std::string	Client::prefix() const
{
	return (_nickname + "!" + _username + "@" + _hostname);
}

// --- registration state ---------------------------------------------------
//
// A client becomes registered by sending PASS, then NICK, then USER
// (docs/ARCHITECTURE.md section 7). Each command flips one flag.

bool	Client::hasPass() const
{
	return (_hasPass);
}

bool	Client::hasNick() const
{
	return (_hasNick);
}

bool	Client::hasUser() const
{
	return (_hasUser);
}

void	Client::setHasPass(bool value)
{
	_hasPass = value;
}

void	Client::setHasNick(bool value)
{
	_hasNick = value;
}

void	Client::setHasUser(bool value)
{
	_hasUser = value;
}

// DERIVED, never stored. There is no _registered member and no
// setRegistered(), because a stored copy can drift out of step with the three
// flags it claims to summarise — and once it does, it lies about the client's
// real state everywhere it is read. Computing it makes disagreement
// impossible.
bool	Client::isRegistered() const
{
	return (_hasPass && _hasNick && _hasUser);
}

// A DIFFERENT question from isRegistered(), which is why it gets its own
// member.
//
// isRegistered() answers "may this client run commands?" and stays true from
// the transition onwards. welcomeSent() answers "has the 001-004 burst already
// gone out?", and that burst must fire exactly ONCE, on the transition. Only a
// stored flag can remember an edge; a derived value only ever knows the level.
bool	Client::welcomeSent() const
{
	return (_welcomeSent);
}

void	Client::setWelcomeSent(bool value)
{
	_welcomeSent = value;
}

int	Client::getFd() const
{
	return (_fd);
}
