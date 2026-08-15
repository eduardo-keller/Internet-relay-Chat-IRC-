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

// --- the loop -------------------------------------------------------------
//
// STEP 1 SHAPE ONLY. It sets the socket up and stays alive long enough to be
// observed with ss(8) and interrupted with Ctrl+C. Step 2 replaces the body
// with the real loop: _pollFds rebuilt from _clients, accept, and the reap.
void	Server::run()
{
	installSignalHandlers();
	setupListenSocket();
	_running = true;
	std::cout << "ircserv: listening on port " << _port << std::endl;

	while (_running && !g_shutdown)
	{
		struct pollfd	pfd;

		pfd.fd = _listenFd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (poll(&pfd, 1, -1) < 0)
		{
			// SIGINT lands here: the signal interrupts poll(), which returns
			// -1 with EINTR. That is not a failure, so retry — and the loop
			// condition then sees the flag and leaves on its own terms.
			if (errno == EINTR)
				continue;
			throw std::runtime_error(std::string("poll: ")
				+ std::strerror(errno));
		}
		if (pfd.revents & POLLIN)
		{
			// accept() arrives in step 2. Leaving rather than looping is
			// deliberate: poll() is level-triggered, so a connection nobody
			// accepts is re-reported immediately, forever, and this loop
			// would spin at 100% CPU.
			std::cout << "ircserv: connection pending, accept() is step 2"
				<< std::endl;
			break;
		}
	}
	std::cout << "ircserv: shutting down" << std::endl;
}
