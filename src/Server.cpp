#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "Channel.hpp"
#include "Client.hpp"
#include "Limits.hpp"
#include "Replies.hpp"
#include "Server.hpp"

// The transport half of the server. See include/Server.hpp for the seam the
// command handlers are allowed to use, and docs/FASE2.md for the order this
// file is being built in.
//
// Everything Channel-facing lives in src/ServerChannels.stub.cpp until the
// domain track ships src/Channel.cpp.

// --- signals --------------------------------------------------------------
//
// Set by the handler, read by run().
//
// volatile sig_atomic_t is the ONLY kind of object a handler may touch. The
// handler can fire between any two machine instructions, so a wider type could
// be read half-written; and without volatile the compiler is free to hoist the
// loop's read of the flag out of the loop and never notice it changed.
//
// The handler does nothing else. Almost nothing is async-signal-safe: a
// handler calling std::cout or delete while main() is inside the allocator
// deadlocks the process. Raising a flag and letting run() act on it at a point
// of its own choosing is the whole technique.
static volatile sig_atomic_t	g_shutdown = 0;

static void	onShutdownSignal(int)
{
	g_shutdown = 1;
}

// --- construction ---------------------------------------------------------

// Deliberately side-effect free: no socket, no syscall, nothing to undo.
//
// That is not tidiness. It is what lets a Server be built on the stack inside
// a unit test, which is how sendToClient's truncation and the whole
// registration state machine get tested with no network at all in steps 4
// and 6. Binding belongs to run(), which is the function that owns the fd's
// lifetime.
Server::Server(int port, const std::string &password) :
	_port(port),
	_password(password),
	_serverName("ircserv"),
	_listenFd(-1),
	_running(false),
	_pollFds(),
	_clients(),
	_channels()
{
}

Server::~Server()
{
	for (std::map<int, Client *>::iterator it = _clients.begin();
		it != _clients.end(); ++it)
	{
		close(it->first);
		delete it->second;
	}
	_clients.clear();
	clearAllChannels();
	if (_listenFd >= 0)
		close(_listenFd);
}

void	Server::stop()
{
	_running = false;
}

const std::string	&Server::getPassword() const
{
	return (_password);
}

const std::string	&Server::getServerName() const
{
	return (_serverName);
}

// --- startup --------------------------------------------------------------

// SIGPIPE FIRST, before any socket exists.
//
// Writing to a socket whose peer already closed raises SIGPIPE, and its
// default action TERMINATES the process — an instant zero, triggered by
// something as ordinary as a client being kill -9'd in the middle of a
// broadcast. Ignored, the same situation makes send() return -1 with EPIPE,
// which is just another error code we already have to check.
//
// SIGINT and SIGTERM only raise the flag. run() notices on its next pass and
// unwinds normally, so ~Server still runs and valgrind stays quiet — calling
// exit() from inside the handler would skip every destructor.
void	Server::installSignalHandlers()
{
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, onShutdownSignal);
	signal(SIGTERM, onShutdownSignal);
}

void	Server::setupListenSocket()
{
	_listenFd = socket(AF_INET, SOCK_STREAM, 0);
	if (_listenFd < 0)
		throw std::runtime_error(std::string("socket: ") + std::strerror(errno));

	// Without SO_REUSEADDR, restarting the server while a previous connection
	// is still in TIME_WAIT fails with EADDRINUSE for up to two minutes. That
	// is not a corner case here — it is every restart during development, and
	// during the evaluation.
	int	yes = 1;
	if (setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
		throw std::runtime_error(std::string("setsockopt: ")
			+ std::strerror(errno));

	// The LISTENING socket is non-blocking too, not just the accepted ones.
	// POLLIN on it means "a connection WAS pending", not "a connection is
	// still pending": the peer can send RST in between, and a blocking
	// accept() would then park the entire server on one client that has
	// already vanished, with every other client frozen behind it.
	if (fcntl(_listenFd, F_SETFL, O_NONBLOCK) < 0)
		throw std::runtime_error(std::string("fcntl: ") + std::strerror(errno));

	struct sockaddr_in	addr;

	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(static_cast<unsigned short>(_port));
	if (bind(_listenFd, reinterpret_cast<struct sockaddr *>(&addr),
			sizeof(addr)) < 0)
		throw std::runtime_error(std::string("bind: ") + std::strerror(errno));

	if (listen(_listenFd, SOMAXCONN) < 0)
		throw std::runtime_error(std::string("listen: ") + std::strerror(errno));
}

// --- the poll loop --------------------------------------------------------
//
// ONE poll() for every fd in the process, the listening socket included. That
// is not a style choice: the subject makes reading or writing any fd outside
// poll() an automatic zero.

void	Server::run()
{
	installSignalHandlers();
	setupListenSocket();
	_running = true;
	std::cout << "ircserv: listening on port " << _port << std::endl;

	while (_running && !g_shutdown)
	{
		buildPollFds();

		// Timeout -1 blocks until something actually happens. There is no
		// periodic work in this server, so any finite timeout would only mean
		// waking up to do nothing. This is what keeps an idle process — even
		// one holding fifty connections — at 0.0% CPU.
		int	ready = poll(&_pollFds[0], _pollFds.size(), -1);

		if (ready < 0)
		{
			// A signal interrupted the wait. Not a failure: go round again and
			// let the loop condition notice g_shutdown on its own terms, so
			// that ~Server still runs.
			if (errno == EINTR)
				continue ;
			throw std::runtime_error(std::string("poll: ")
				+ std::strerror(errno));
		}

		// _pollFds is NOT touched inside this loop — acceptNewClient does not
		// push into it, and nothing is deleted before reapDisconnected. Both
		// facts are what make plain indexing safe here.
		for (std::size_t i = 0; i < _pollFds.size(); ++i)
		{
			short	re = _pollFds[i].revents;
			int		fd = _pollFds[i].fd;

			if (re == 0)
				continue ;
			if (fd == _listenFd)
			{
				if (re & POLLIN)
					acceptNewClient();
				continue ;
			}

			// POLLIN IS HANDLED BEFORE THE ERROR FLAGS, on purpose. A peer that
			// sent FIN is half closed, not gone: bytes it wrote before closing
			// are still sitting in our kernel buffer, and acting on the hangup
			// first would throw away a QUIT we were supposed to see.
			if (re & POLLIN)
				handleReadable(fd);
			if (re & POLLOUT)
				handleWritable(fd);
			if (re & (POLLERR | POLLHUP | POLLNVAL))
			{
				// POLLNVAL means the fd in OUR array is not open — a bookkeeping
				// bug on our side. Dropping the client is still the right move:
				// left in place, poll() would report it every iteration forever.
				Client	*client = findClientByFd(fd);

				if (client != NULL)
					disconnectClient(*client, "Connection closed by peer");
			}
		}

		// The ONE place a Client is deleted, and it runs after every event of
		// this iteration has been handled — so no code above is still holding a
		// reference to what is about to be freed.
		reapDisconnected();
	}
	std::cout << "ircserv: shutting down" << std::endl;
}

// Rebuilt from scratch every iteration rather than kept in sync incrementally.
//
// The incremental version has to match every insertion and erase against the
// indices the event loop is currently holding; one missed shift and the loop
// reads another client's revents, or worse, a closed fd's. Rebuilding makes
// that class of bug unreachable by construction. The cost is O(n) over a
// handful of fds per wait — invisible next to the syscall it precedes.
void	Server::buildPollFds()
{
	struct pollfd	pfd;

	_pollFds.clear();
	pfd.fd = _listenFd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	_pollFds.push_back(pfd);
	for (std::map<int, Client *>::const_iterator it = _clients.begin();
		it != _clients.end(); ++it)
	{
		pfd.fd = it->first;
		pfd.events = POLLIN;
		// POLLOUT ONLY WHEN THERE IS SOMETHING TO WRITE. A socket with room in
		// its send buffer is always writable, so arming it unconditionally
		// makes poll() return immediately every single iteration and the loop
		// spins at 100% CPU with nothing to do — the most common way this
		// design goes wrong, and the first thing an evaluator measures.
		if (it->second->hasPendingOutput())
			pfd.events |= POLLOUT;
		pfd.revents = 0;
		_pollFds.push_back(pfd);
	}
}

// One accept() per readiness event, per the one-syscall-per-event rule in
// ARCHITECTURE.md section 11: poll() is level-triggered, so a second connection
// waiting in the backlog is simply reported again on the next pass. Nothing is
// lost and no client can starve the others by connecting in a tight loop.
void	Server::acceptNewClient()
{
	struct sockaddr_in	addr;
	socklen_t			addrLen = sizeof(addr);

	std::memset(&addr, 0, sizeof(addr));

	int	fd = accept(_listenFd, reinterpret_cast<struct sockaddr *>(&addr),
			&addrLen);

	if (fd < 0)
	{
		// POLLIN said a connection WAS pending. Between that and this call the
		// peer can have sent RST and disappeared, which leaves accept() with
		// EAGAIN. That is an ordinary event, not a failure — and it is exactly
		// why the listening socket itself is non-blocking: a blocking accept()
		// here would park the whole server on a client that no longer exists.
		return ;
	}

	// EVERY accepted fd is made non-blocking. Inheriting O_NONBLOCK from the
	// listening socket is NOT portable — Linux does not do it — so a missing
	// fcntl here means one slow client can block send() and freeze everyone.
	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
	{
		close(fd);
		return ;
	}

	char		host[INET_ADDRSTRLEN];
	const char	*text = inet_ntop(AF_INET, &addr.sin_addr, host, sizeof(host));
	std::string	hostname(text != NULL ? text : "unknown");

	// The dotted-quad address is used as the hostname, with no reverse DNS.
	// Resolving would need a blocking lookup inside the event loop, freezing
	// every other client for the length of a DNS timeout.
	_clients[fd] = new Client(fd, hostname);
	std::cout << "ircserv: accepted fd " << fd << " from " << hostname
		<< std::endl;
}

Client	*Server::findClientByFd(int fd)
{
	std::map<int, Client *>::iterator	it = _clients.find(fd);

	if (it == _clients.end())
		return (NULL);
	return (it->second);
}

// The read path: ONE recv() per readiness event, into the client's buffer, then
// drain every complete line the buffer now holds.
//
// The single recv() is not a shortcut. poll() is level-triggered, so bytes left
// in the kernel are reported again on the very next pass: nothing is lost and
// nothing stalls. Looping until EAGAIN is mandatory only for edge-triggered
// epoll, and here it would let one flooding client monopolise the loop.
// ARCHITECTURE.md section 11 has the long version.
//
// recv() returning 0 is also the ONLY reliable sign that a peer closed: on
// Linux that surfaces as POLLIN, not POLLHUP, which is reported only once both
// directions are shut down.
void	Server::handleReadable(int fd)
{
	Client	*client = findClientByFd(fd);

	if (client == NULL || client->isDisconnecting())
		return ;

	char	buf[irc::RECV_CHUNK];
	ssize_t	n = recv(fd, buf, sizeof(buf), 0);

	if (n == 0)
	{
		// End of stream: the peer called close() or shutdown(). This is the
		// ONLY reliable indication of an orderly disconnect.
		disconnectClient(*client, "Client closed connection");
		return ;
	}
	if (n < 0)
	{
		// EAGAIN/EWOULDBLOCK on a non-blocking fd means "nothing right now" —
		// neither an error nor a disconnect. Treating it as failure is the
		// classic non-blocking bug, and it is reachable here even with
		// level-triggered poll(): a checksum-failed segment can raise POLLIN
		// and then be discarded before we read. EINTR is a signal landing
		// mid-call; the next poll() will tell us again.
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			return ;
		disconnectClient(*client, std::strerror(errno));
		return ;
	}

	// THE TWO-ARGUMENT std::string CONSTRUCTOR, always. std::string(buf) would
	// stop at the first NUL byte and silently drop the rest of the packet —
	// and recv() gives no terminator, so the one-argument form would also read
	// past n into uninitialised stack memory.
	if (!client->appendToReadBuffer(std::string(buf, static_cast<std::size_t>(n))))
	{
		// The buffer passed MAX_READ_BUFFER with no complete line in it: one
		// unterminated flood, not legitimate pipelining. The reason string is
		// what step 4 will put on the wire as "ERROR :Request too long".
		disconnectClient(*client, "Request too long");
		return ;
	}

	// One packet can carry several commands, or half of one. This is where
	// that stops mattering: extractCommand pops exactly one complete line per
	// call and leaves any partial remainder in the buffer for the next recv.
	//
	// isDisconnecting() IS CHECKED FIRST, EVERY TIME. From step 5 on, a line
	// can be a QUIT, and "QUIT :bye\r\nPRIVMSG #c :x\r\n" arrives in a single
	// packet all the time. Without this guard the loop would keep feeding
	// lines to a client already marked for death — the use-after-free the
	// deferred disconnect exists to prevent.
	std::string	line;

	while (!client->isDisconnecting() && client->extractCommand(line))
		handleLine(*client, line);
}

// TEMPORARY — step 5 replaces this body with the real dispatcher (parse,
// table lookup, 421, the registration gate). It exists now only so that the
// reassembly above is observable before any command handler exists.
//
// The echo goes out through sendToClient, like every other byte this server
// emits, so it is truncated and CRLF-terminated by the same code the real
// replies will use. The log line stays because tests/it/read_path.sh asserts
// on it.
void	Server::handleLine(Client &client, const std::string &line)
{
	std::cout << "ircserv: line from " << client.getHostname() << " ["
		<< line << "] (" << line.size() << " bytes)" << std::endl;

	// SCAFFOLD, and it dies with the rest of this function in step 5.
	//
	// "JOIN #chan" is the only way to get a client into a channel before
	// cmdJoin exists, and without one the disconnect sweep has no end-to-end
	// test at all. The bug that sweep prevents is a use-after-free — Channel
	// holds non-owning Client* — and reading the code is not the same as
	// reproducing the failure under valgrind. tests/it/channel_seam.sh does
	// exactly that: two clients in a channel, one killed, the other then
	// speaking, which walks the member set the dead one was in.
	if (line.size() > 5 && line.compare(0, 5, "JOIN ") == 0)
	{
		Channel	*channel = getOrCreateChannel(line.substr(5));

		channel->addMember(&client);
		sendToClient(client, "joined " + channel->getName());
		return ;
	}

	// Dereferences every peer pointer, which is the trap: a client deleted
	// while still listed as a member would be read right here.
	broadcastToPeers(client, "peer said :" + line, false);
	sendToClient(client, line);
}

// --- the write path -------------------------------------------------------

// THE SINGLE CHOKE POINT for every byte this server emits, which is what makes
// one truncation here enough for every command.
//
// The order matters: truncate to 510 FIRST, then append CRLF, giving 512 on
// the wire — the RFC 2812 section 2.3 limit, CRLF included. Truncating after
// would cut the "\r\n" off a maximum-length line and glue two messages
// together in the client's parser.
//
// Checking only the INCOMING length would miss this entirely. A client sends
// "PRIVMSG #chan :<504 bytes>", perfectly legal at under 512; the server
// relays it as ":nick!user@host PRIVMSG #chan :<504 bytes>" and the prefix it
// just added pushes the result past the limit.
void	Server::sendToClient(Client &client, const std::string &line)
{
	std::string	payload(line);

	if (payload.size() > irc::MAX_PAYLOAD_LEN)
		payload.resize(irc::MAX_PAYLOAD_LEN);
	payload += "\r\n";

	// queueOutput refuses when the queue passed MAX_OUTPUT_QUEUE: the client
	// stopped reading (suspended with Ctrl+Z, or hostile) while traffic kept
	// arriving. Other servers call this SendQ exceeded. Note that this is the
	// call that can re-enter disconnectClient — see the guard there.
	if (!client.queueOutput(payload))
		disconnectClient(client, "SendQ exceeded");
}

// One send() per readiness event, mirroring the read side. send() reports how
// many bytes the kernel accepted, which is NOT necessarily all of them, and
// POLLOUT stays armed while anything remains queued — so the remainder goes
// out on the next pass with no loop needed here.
void	Server::handleWritable(int fd)
{
	Client	*client = findClientByFd(fd);

	if (client == NULL || !client->hasPendingOutput())
		return ;

	const std::string	&pending = client->getOutputBuffer();
	ssize_t				n = send(fd, pending.data(), pending.size(), 0);

	if (n < 0)
	{
		// A full kernel send buffer is EAGAIN, and it is the NORMAL case for a
		// slow client: the data stays queued and POLLOUT fires again when the
		// window opens. Treating it as an error would disconnect exactly the
		// clients this buffering exists to serve.
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			return ;
		// EPIPE lands here rather than killing the process, because SIGPIPE is
		// ignored at startup.
		disconnectClient(*client, std::strerror(errno));
		return ;
	}
	client->consumeOutput(static_cast<std::size_t>(n));
}

// Marks; never deletes. See ARCHITECTURE.md section 4 for why the delete is
// deferred to reapDisconnected.
void	Server::disconnectClient(Client &client, const std::string &reason)
{
	// This guard is not an optimisation. From step 4 on, this function queues
	// "ERROR :<reason>", and queueing fails on a client whose SendQ is already
	// full — whose failure path is another call to disconnectClient. Without
	// the early return that is unbounded recursion until the stack is gone.
	if (client.isDisconnecting())
		return ;

	// MARKED BEFORE ANYTHING IS QUEUED. sendToClient below can fail on a full
	// queue and call straight back into this function; the mark is what makes
	// that second call return at the line above instead of recursing forever.
	client.markDisconnecting(reason);

	// THE PEERS ARE TOLD WHILE THE CLIENT IS STILL A MEMBER. sweepChannels
	// runs later, in reapDisconnected, so the recipient set is still complete
	// here — do this after the sweep and the QUIT reaches nobody.
	//
	// Only for a registered client: one that never got past PASS/NICK/USER has
	// no nickname to build a prefix from, and no peers to tell either.
	// includeOrigin is false — a client does not need its own QUIT echoed, and
	// it is about to receive the ERROR below instead.
	if (client.isRegistered())
		broadcastToPeers(client,
			irc::fromClient(client.prefix(), "QUIT", ":" + reason), false);

	sendToClient(client, "ERROR :" + reason);
}

// The single point where a Client is destroyed, called at the END of a poll
// iteration and nowhere else.
void	Server::reapDisconnected()
{
	std::map<int, Client *>::iterator	it = _clients.begin();

	while (it != _clients.end())
	{
		if (!it->second->isDisconnecting())
		{
			++it;
			continue ;
		}

		Client	*client = it->second;

		std::cout << "ircserv: closing fd " << it->first << " ("
			<< client->getQuitReason() << ")" << std::endl;

		// BEST-EFFORT FLUSH, while the fd is still open. This is the only
		// reason the disconnect is deferred rather than immediate: it is what
		// gets "ERROR :<reason>" — and the 464 before it, and the QUIT echo —
		// onto the wire at all. Once close() runs, anything still queued is
		// gone.
		//
		// One send, no retry and no waiting for POLLOUT: the client is leaving,
		// and a peer that has stopped reading must not be able to hold a slot
		// in the loop by refusing to drain. If the kernel takes only part of
		// it, the rest is lost, which is the correct trade here.
		if (client->hasPendingOutput())
		{
			const std::string	&pending = client->getOutputBuffer();
			ssize_t				sent = send(it->first, pending.data(),
									pending.size(), 0);

			if (sent > 0)
				client->consumeOutput(static_cast<std::size_t>(sent));
		}

		// sweepChannels is a no-op until step 4.5, and it MUST stay ahead of
		// the delete: Channel holds non-owning Client*, so a client freed while
		// still listed as a member leaves dangling pointers in every channel it
		// joined.
		sweepChannels(*client);
		close(it->first);

		// std::map::erase invalidates only the erased iterator, and in C++98
		// it returns void — hence copy, advance, then erase the copy. Erasing
		// first and advancing afterwards would advance a dead iterator.
		std::map<int, Client *>::iterator	dead = it;

		++it;
		_clients.erase(dead);
		delete client;
	}
}
