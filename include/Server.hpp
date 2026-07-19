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
		//
		// This is the single choke point for every byte the server emits, so
		// it is also where the 512-byte limit is enforced: `line` is
		// truncated to irc::MAX_PAYLOAD_LEN before the CRLF is appended.
		// That matters because a PRIVMSG that was legal on the way IN can
		// exceed 512 on the way OUT once ":nick!user@host " is prepended.
		void	sendToClient(Client &client, const std::string &line);

		// Sends to every member of `channel`. When `except` is non-NULL that
		// client is skipped — used for PRIVMSG, where the sender must not
		// receive their own message back.
		void	broadcastToChannel(Channel &channel, const std::string &line,
					const Client *except);

		// Sends to every client sharing at least one channel with `origin`,
		// each recipient exactly ONCE however many channels they share.
		// NICK and QUIT need this; looping broadcastToChannel over the
		// origin's channels would deliver duplicates to anyone in two of
		// them. `includeOrigin` is true for NICK (irssi wants its own change
		// echoed back) and false for QUIT.
		void	broadcastToPeers(const Client &origin, const std::string &line,
					bool includeOrigin);

		// Marks the client for disconnection; it is NOT deleted here.
		//
		// The client is flagged, the QUIT is broadcast to its peers, and
		// "ERROR :<reason>" is queued. The poll loop then reaps every marked
		// client at the END of the iteration: one best-effort send() to flush
		// what is queued, remove from all channels and invite lists, close,
		// delete.
		//
		// Deleting immediately would be a use-after-free. One TCP packet can
		// carry "QUIT\r\nPRIVMSG #c :hi\r\n"; the dispatch loop is still
		// holding this Client& and still has lines to process. Deferring the
		// delete also means queued output can actually be flushed, which is
		// impossible once the fd is closed.
		//
		// The Client& REMAINS VALID on return. The handler should return
		// promptly, and the dispatch loop must stop feeding this client
		// further lines once isDisconnecting() is true.
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
		// Runs once at the end of every poll iteration, after all events are
		// handled: flushes, closes and deletes every client marked by
		// disconnectClient. The only place a Client is ever deleted.
		void	reapDisconnected();

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
