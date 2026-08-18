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

#include "Client.hpp"
#include "Limits.hpp"
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
			if (re & (POLLERR | POLLHUP | POLLNVAL))
			{
				// POLLNVAL means the fd in OUR array is not open — a bookkeeping
				// bug on our side. Dropping the client is still the right move:
				// left in place, poll() would report it every iteration forever.
				Client	*client = findClientByFd(fd);

				if (client != NULL)
					disconnectClient(*client, "Connection closed by peer");
			}
			// POLLOUT joins here in step 4, once sendToClient exists and
			// buildPollFds has something to arm the flag for.
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
		// POLLOUT is deliberately absent. A socket with room in its send
		// buffer is ALWAYS writable, so arming it unconditionally makes
		// poll() return immediately every single iteration and the loop spins
		// at 100% CPU. Step 4 arms it only when hasPendingOutput() is true.
		pfd.events = POLLIN;
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

// STEP 2 VERSION: the syscall and its error taxonomy, with the payload thrown
// away. Step 3 replaces the discard with appendToReadBuffer() plus the drain
// loop; nothing above that line changes.
//
// The recv() cannot wait for step 3. On Linux a peer closing normally shows up
// as POLLIN with recv() returning 0 — NOT as POLLHUP, which is only reported
// once both directions are shut down. So without this call a client that
// simply quits is never noticed: its POLLIN stays raised, poll() returns
// instantly forever, and the loop burns 100% CPU on a socket nobody reads.
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

	// STEP 3 GOES HERE: if (!client->appendToReadBuffer(std::string(buf, n)))
	// -> "ERROR :Request too long" + disconnect; then drain complete lines
	// with extractCommand while !isDisconnecting().
	std::cout << "ircserv: fd " << fd << " sent " << n
		<< " bytes (discarded until step 3)" << std::endl;
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
	client.markDisconnecting(reason);
	// Step 4 queues "ERROR :" + reason here; step 4.5 adds the QUIT broadcast
	// to the client's peers. Both need machinery that does not exist yet.
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

		// Step 4 adds the best-effort flush of the output queue here, while
		// the fd is still open. sweepChannels is a no-op until step 4.5, and
		// it MUST stay ahead of the delete: Channel holds non-owning Client*,
		// so a client freed while still listed as a member leaves dangling
		// pointers in every channel it joined.
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
