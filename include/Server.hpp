#ifndef SERVER_HPP
#define SERVER_HPP

#include <map>
#include <string>
#include <vector>

#include <poll.h>

class Channel;
class Client;

// THE SEAM between the two tracks.
//
// The public section below is the *only* thing a command handler may call to
// reach the outside world. Handlers never see an fd, never call send() or
// recv(), never touch _pollFds. Everything under "private" belongs to the
// transport track and may change without renegotiating anything.
//
// Keep this surface small: every public method here is a coupling point
// between the two devs. Adding one is a conversation, not a commit.
class Server
{
	public:
		Server(int port, const std::string &password);
		~Server();

		// Binds, listens, and runs the single poll() loop until stopped.
		void	run();
		void	stop();

		// --- lookup -------------------------------------------------------
		// Return NULL when not found. Nickname lookup is case-insensitive
		// (utils::equalsIgnoreCase), channel lookup likewise.
		Client	*findClientByNick(const std::string &nickname);
		Channel	*findChannel(const std::string &name);
		// Creates the channel if absent. The caller is responsible for making
		// the first member an operator.
		Channel	*getOrCreateChannel(const std::string &name);
		// Called after the last member leaves. Deletes the Channel.
		void	removeChannel(const std::string &name);

		// --- delivery -----------------------------------------------------
		// CONVENTION: `line` is passed WITHOUT its line ending. sendToClient
		// appends "\r\n" itself. Never put CRLF in a string you pass here or
		// you will send a blank line and confuse irssi.
		void	sendToClient(Client &client, const std::string &line);
		// Sends to every member of `channel`. When `except` is non-NULL that
		// client is skipped — used for PRIVMSG, where the sender must not
		// receive their own message back.
		void	broadcastToChannel(Channel &channel, const std::string &line,
					const Client *except);

		// Queues the quit notice, closes the fd, removes the client from
		// every channel, and deletes it. The Client& is INVALID on return, so
		// a handler must return immediately after calling this.
		void	disconnectClient(Client &client, const std::string &reason);

		const std::string	&getPassword() const;
		const std::string	&getServerName() const;

	private:
		// Server owns its clients and channels and is not copyable. Declared
		// private and left undefined: the C++98 way to forbid copying.
		Server(const Server &other);
		Server	&operator=(const Server &other);

		// --- transport track internals; not part of the contract ----------
		void	setupListenSocket();
		void	acceptNewClient();
		void	handleReadable(int fd);
		void	handleWritable(int fd);
		void	handleLine(Client &client, const std::string &line);

		int								_port;
		std::string						_password;
		std::string						_serverName;
		int								_listenFd;
		bool							_running;
		std::vector<struct pollfd>		_pollFds;
		std::map<int, Client *>			_clients;
		std::map<std::string, Channel *>	_channels;
};

#endif
