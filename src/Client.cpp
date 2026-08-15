#include "Client.hpp"
#include "Limits.hpp"

#include <algorithm>

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

// --- read side ------------------------------------------------------------
//
// TCP delivers a BYTE STREAM, not messages. It promises ordering and delivery
// and says nothing about grouping: recv() hands back however many bytes happen
// to be available, with no relation to how the sender chopped up its send()
// calls. So the problem runs in both directions, and the docs carry one example
// of each —
//
//   fragmented: nc sends "com", "man", "d\n" as three writes and the server
//               must see ONE command (the subject, chapter III.3)
//   coalesced:  irssi sends PASS, NICK and USER back to back, often inside a
//               single packet, and the server must see THREE
//               (docs/ARCHITECTURE.md section 7)
//
// appendToReadBuffer answers the first, extractCommand the second, and neither
// is much use without the other.
//
// The buffer is a MEMBER, not a global, because poll() interleaves reads across
// clients: one shared buffer would splice one client's half-line onto another's.

// Accumulates whatever recv() produced, however it arrived.
//
// The caller must build the string from recv()'s byte COUNT —
// std::string(buf, n), never std::string(buf). buf is not NUL-terminated, so
// the one-argument form runs past the data until it stumbles onto a zero byte,
// and it truncates at any NUL the client genuinely sent. Check n before
// converting, too: n == 0 is the peer closing the connection, and
// std::string(buf, -1) asks for a string of roughly 18 quintillion bytes.
//
// Returns FALSE when the buffer has grown past MAX_READ_BUFFER WITH NO
// COMPLETE LINE IN IT. That qualifier is the whole rule, not a refinement of
// it. 4096 bytes containing finished lines is pipelining — irssi and any pasted
// block of commands produce it legitimately, and disconnecting over it punishes
// a well-behaved client. 4096 bytes with nothing finished inside them is a
// single unterminated flood: memory exhaustion driven by a client that has done
// nothing structurally wrong, only nothing complete. The cap is on the
// ACCUMULATED buffer, so a slow drip trips it exactly like one huge write.
//
// The test is find('\n'), deliberately the same criterion extractCommand frames
// on. Any other test here would eventually refuse a buffer that extractCommand
// would have drained perfectly happily.
//
// APPEND FIRST, CHECK AFTER. Refusing before appending could throw away the
// very chunk that completed a line: a buffer sitting at 4090 bytes receiving
// the last 10 plus a CRLF is entirely legitimate. Appending first costs a
// bounded transient — recv() asks for at most RECV_CHUNK, so the buffer peaks
// at MAX_READ_BUFFER + RECV_CHUNK before we answer, which is explainable rather
// than unbounded.
//
// Phase 2 acts on the false: handleReadable sends "ERROR :Request too long" and
// calls disconnectClient. Nothing here touches an fd — returning false IS how
// logic asks for a disconnect.
bool	Client::appendToReadBuffer(const std::string &data)
{
	_readBuffer += data;
	if (_readBuffer.size() > irc::MAX_READ_BUFFER
		&& _readBuffer.find('\n') == std::string::npos)
		return (false);
	return (true);
}

// Pops one complete line, terminator removed, and reports through the RETURN
// VALUE whether there was one.
//
// The out-parameter is not merely C++98 lacking std::optional. An empty line is
// a legal COMPLETE line, so "" cannot double as "nothing available": the two
// mean opposite things to the caller, since no line means stop draining and
// wait for more bytes, while an empty line means discard it and keep draining.
// Same reasoning as Message::hasTrailing, docs/ARCHITECTURE.md section 5.
//
// We search for '\n' and then drop a trailing '\r', instead of searching for
// "\r\n" outright. The RFC does say CRLF, but nc without -C sends a bare LF and
// the subject's test is written with nc, so a server demanding CRLF never
// answers it — and worse, those unterminated bytes would pile up until the
// read-buffer cap disconnected a perfectly well-behaved client.
//
// The + 1 in erase() is the terminator itself. Dropping it is a quiet bug: the
// stray '\n' would come back as an extra empty line in front of every real one,
// and since the dispatcher ignores empty lines the server would still look like
// it works.
//
// The order of the last three statements is the design, not an accident:
// framing first (which bytes make up this line), then the terminator (the '\r'
// belongs to the CRLF, and RFC 2812 counts the CRLF inside its 512), and only
// then the 510 the payload is left with. Sanitising will come after all three
// and may shorten the line further, because the limit is about bytes on the
// wire, not about how many of them survive cleaning.
//
// Truncating is deliberate policy rather than laziness. The line is COMPLETE
// and the client is merely verbose, so real servers shorten it and carry on
// instead of dropping the connection — the opposite of the read-buffer cap,
// where there is no complete line to salvage. Note the discarded tail is
// already out of the buffer by the time resize() runs, because erase() took the
// whole line: leaving the excess behind would hand the parser a second, garbage
// command built out of it.

// The three bytes stripped from every extracted line, and ONLY these three.
//
// "Strip every control byte below 0x20" is the tempting safe-looking choice and
// it is wrong: IRC carries meaning down there. 0x01 delimits CTCP, which is how
// irssi implements /me (it sends "PRIVMSG #chan :\x01ACTION waves\x01"), and
// 0x02, 0x03, 0x0F and 0x1F are bold, colour, reset and underline. We never
// implement CTCP — we only relay the bytes — so /me works for free unless we
// mangle it. Stripping broadly would render it as the literal text
// "ACTION waves" between two real irssi clients in phase 3.
static bool	isStrippedByte(char c)
{
	return (c == '\0' || c == '\r' || c == '\n');
}

// Why any of this is needed: std::string stores a NUL perfectly well, but
// .c_str() stops dead at it, and both live in the same program. A line arriving
// as "PING\0 :token" is 12 bytes to size() and 4 bytes to anything C-shaped, so
// the dispatch map misses and answers 421 while a debug print shows "PING". The
// bytes are right and your eyes are wrong, with nothing on screen to say so.
//
// An embedded '\r' is the other one: it survives the terminator strip above,
// jumps the recipient's cursor to column 0, and desyncs any parser lenient
// enough to treat a bare CR as a line ending. A full CRLF injection is already
// impossible here — the line was cut at the first '\n', so none can be inside
// it — which means '\n' in this predicate is defence against a future change to
// the framing, not a live case.
//
// Sanitising comes LAST, after the cap, and that order is a decision. Cleaning
// first would let a client pad with NULs to smuggle payload past the 510: send
// 600 bytes of which 100 are NUL and clean-first delivers 500 real bytes for
// the same cost on the wire. Anchoring the cap to bytes ON THE WIRE keeps it a
// real cap. The accepted price is that a delivered line can now be SHORTER
// than 510.
//
// erase(remove_if(...), end()) is the erase-remove idiom. remove_if erases
// nothing: it shifts the kept bytes forward and returns an iterator to the new
// logical end, leaving whatever it likes in the tail. The erase() is what
// actually shortens the string, and calling remove_if on its own — the classic
// misuse — would leave size() completely unchanged.
bool	Client::extractCommand(std::string &out)
{
	const std::string::size_type	end = _readBuffer.find('\n');

	if (end == std::string::npos)
		return (false);
	out = _readBuffer.substr(0, end);
	_readBuffer.erase(0, end + 1);
	if (!out.empty() && out[out.size() - 1] == '\r')
		out.erase(out.size() - 1);
	if (out.size() > irc::MAX_PAYLOAD_LEN)
		out.resize(irc::MAX_PAYLOAD_LEN);
	out.erase(std::remove_if(out.begin(), out.end(), isStrippedByte),
		out.end());
	return (true);
}

// --- write side -----------------------------------------------------------
//
// A non-blocking send() reports a byte COUNT, not success or failure. It may
// accept 200 bytes of a 500-byte line and leave the rest, and that remainder
// has nowhere to go except a buffer we own — per client, for the same reason
// the read buffer is per client: poll() multiplexes, and every connection has
// its own unfinished business.
//
// This side is deliberately ignorant of the protocol. It queues bytes, nothing
// more. The CRLF and the 510-byte truncation both belong to
// Server::sendToClient (docs/ARCHITECTURE.md sections 2 and 11), the single
// chokepoint every outbound byte passes through. Appending the CRLF here as
// well would put a blank line after every message.

// Returns FALSE when this message would push the queue past MAX_OUTPUT_QUEUE —
// what other servers call "SendQ exceeded". The client has stopped reading:
// Ctrl+Z on nc, a laptop lid closing, or plain hostility.
//
// Note the asymmetry with the read side. A flooder there pays for its own flood
// by having to transmit the bytes; here the stalled client turns OTHER PEOPLE'S
// traffic into our memory, since every PRIVMSG to a busy channel queues another
// line for it. Not reading costs the attacker nothing, which makes this the
// more dangerous of the two caps.
//
// CHECK FIRST, APPEND AFTER — the opposite of appendToReadBuffer, for a reason
// rather than by taste. There, the arriving chunk might COMPLETE a line, so
// refusing before looking could throw away the very bytes that made the buffer
// legal. Here nothing is salvageable: the client is not reading, it is about to
// be reaped, and these bytes will never leave the machine. With no payoff there
// is no reason to accept a transient, so 65536 is a HARD cap — never exceeded,
// not even briefly. Refusing without appending also leaves the queue intact and
// still drainable, which is what the reap's best-effort flush needs.
//
// The test is on the SUM, not on the current size. Checking _outputBuffer.size()
// alone looks equivalent and is not: a queue sitting at 65535 would accept a
// message of any length and land wherever that message put it, making the cap
// soft again — exceedable by up to one whole message. No overflow risk in the
// addition, since data is a single line of at most 512 bytes once sendToClient
// has truncated it.
//
// There is no qualifier here, unlike the read cap. A large read buffer is
// ambiguous — unterminated flood, or legitimate pipelining? — which is exactly
// what that find('\n') test resolves. A large output queue is not ambiguous: no
// client that is reading normally accumulates 64 KB of undelivered data, so the
// size IS the symptom and there is nothing to disambiguate.
//
// Phase 2 acts on the false: sendToClient calls disconnectClient. See
// docs/ARCHITECTURE.md section 4 for why disconnectClient must return early when
// the client is already marked — it queues ERROR :<reason>, which lands right
// back here and would recurse.
bool	Client::queueOutput(const std::string &data)
{
	if (_outputBuffer.size() + data.size() > irc::MAX_OUTPUT_QUEUE)
		return (false);
	_outputBuffer += data;
	return (true);
}

// A const reference, so the poll loop hands .c_str() and .size() straight to
// send() with no copy. Const also because consumption must go through
// consumeOutput: this class owns the invariant that what the kernel accepted is
// exactly what gets dropped, and a caller editing the buffer directly could
// desync the two.
const std::string	&Client::getOutputBuffer() const
{
	return (_outputBuffer);
}

// Drops the bytes send() actually accepted.
//
// The clamp is belt-and-braces: std::string::erase already removes only
// min(n, size() - pos), so erase(0, 999999) on a short buffer is well defined
// and simply clears it. It stays because the bound is then visible in the code
// rather than resting on a corner of the standard the reader has to recall, and
// because it would become load-bearing the moment this stopped being a
// std::string.
void	Client::consumeOutput(std::size_t bytes)
{
	if (bytes > _outputBuffer.size())
		bytes = _outputBuffer.size();
	_outputBuffer.erase(0, bytes);
}

// The poll loop arms POLLOUT ONLY while this is true.
//
// That is not an optimisation. A socket with room in its send buffer is almost
// always writable, so a permanently armed POLLOUT makes poll() return
// immediately every iteration and the loop spins at 100% CPU with nothing to
// do. It is visible in top and it is one of the first things an evaluator
// notices. See docs/ARCHITECTURE.md section 11.
bool	Client::hasPendingOutput() const
{
	return (!_outputBuffer.empty());
}

// --- deferred disconnect --------------------------------------------------
//
// Dropping a client happens in two steps: this marks it, and the poll loop's
// reapDisconnected does the real work at the END of the iteration — flush what
// is still queued, remove it from every channel and invite list, close, delete.
// See docs/ARCHITECTURE.md section 4.
//
// Deleting on the spot instead would be a use-after-free, and the path to it is
// ordinary rather than exotic. Server::handleReadable drains a client with
//
//     while (client.extractCommand(line))
//         dispatch(server, client, parseMessage(line));
//
// so the loop CONDITION is evaluated again the moment a handler returns. A lone
// QUIT is already enough: cmdQuit returns, and extractCommand is called on
// freed memory. A single packet carrying "QUIT :bye\r\nPRIVMSG #chan :hi\r\n"
// is worse still, because the PRIVMSG would then be dispatched through a
// dangling reference.
//
// Deferring buys a second thing. The reason stored here still has to leave as
// "ERROR :<reason>", and a closed fd cannot carry it — so the decision to drop
// a client has to outlive the handler that made it.

bool	Client::isDisconnecting() const
{
	return (_disconnecting);
}

// The FIRST reason wins: a later mark cannot overwrite it.
//
// A client dropped for a bad password is marked "Password incorrect". Any
// generic failure that follows in the same iteration — a short send, a recv
// returning 0 — would otherwise replace the cause with its own consequence,
// leaving ERROR :<reason> reporting the symptom and discarding the only
// diagnosis there was.
//
// It also makes the call idempotent, so no caller has to guard with
// if (!client.isDisconnecting()) first. That guard lives here, once, instead of
// at every call site where someone eventually forgets it.
void	Client::markDisconnecting(const std::string &reason)
{
	if (!_disconnecting)
	{
		_disconnecting = true;
		_quitReason = reason;
	}
}

const std::string	&Client::getQuitReason() const
{
	return (_quitReason);
}

int	Client::getFd() const
{
	return (_fd);
}
