#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <cstddef>
#include <string>

// One connected user.
//
// Owned by Server (Server creates and deletes every Client). Channel holds
// non-owning Client* pointers, so a Client must be removed from every channel
// BEFORE it is deleted.
//
// The read buffer lives here, but its implementation is PRIVATE to the
// transport track: only appendToReadBuffer() and extractCommand() are part of
// the contract. Whether the internals stay a std::string or become a dedicated
// buffer class is the transport owner's call and needs no renegotiation.
//
// getFd() is private and Server is a friend: that makes "logic never touches
// file descriptors" a COMPILE ERROR rather than a convention. A command
// handler physically cannot reach the fd.
class Client
{
	public:
		Client();
		explicit Client(int fd, const std::string &hostname);
		Client(const Client &other);
		Client	&operator=(const Client &other);
		~Client();

		const std::string	&getNickname() const;
		const std::string	&getUsername() const;
		const std::string	&getRealname() const;
		const std::string	&getHostname() const;

		void	setNickname(const std::string &nickname);
		void	setUsername(const std::string &username);
		void	setRealname(const std::string &realname);

		// --- registration state -------------------------------------------
		// isRegistered() is DERIVED, not stored: it returns
		// _hasPass && _hasNick && _hasUser. There is deliberately no
		// setRegistered(), because a stored copy can disagree with the three
		// flags and then lies about the client's real state.
		//
		// What genuinely needs remembering is whether we already sent the
		// 001-004 burst, since that must happen exactly once on the
		// transition into registered. That is welcomeSent(), and it is a
		// different question from isRegistered().
		bool	hasPass() const;
		bool	hasNick() const;
		bool	hasUser() const;
		bool	isRegistered() const;
		void	setHasPass(bool value);
		void	setHasNick(bool value);
		void	setHasUser(bool value);

		bool	welcomeSent() const;
		void	setWelcomeSent(bool value);

		// "nick!user@host" — the source prefix of every message this client
		// originates. Used by JOIN/PART/PRIVMSG/KICK/QUIT echoes.
		std::string	prefix() const;

		// --- read side: pure logic, no recv() in here ---------------------
		// Transport appends whatever recv() returned, however fragmented.
		// Returns FALSE when the buffer exceeded irc::MAX_READ_BUFFER without
		// containing a complete line — a single unterminated flood. The
		// caller must then disconnect the client. Check the return value.
		bool	appendToReadBuffer(const std::string &data);
		// Pops one complete line (up to and including CRLF or a bare LF) and
		// returns it WITHOUT the line ending. Returns false when the buffer
		// holds no complete line yet, which is the partial-packet case.
		// A line longer than irc::MAX_PAYLOAD_LEN is truncated to it.
		bool	extractCommand(std::string &out);

		// --- write side: pure logic, no send() in here ---------------------
		// Non-blocking send() may accept only part of a write, so outbound
		// data is queued here and drained by the poll loop under POLLOUT.
		// Returns FALSE when the queue exceeded irc::MAX_OUTPUT_QUEUE: the
		// client has stopped reading and must be disconnected.
		bool				queueOutput(const std::string &data);
		const std::string	&getOutputBuffer() const;
		void				consumeOutput(std::size_t bytes);
		bool				hasPendingOutput() const;

		// --- deferred disconnect ------------------------------------------
		// Server::disconnectClient marks the client instead of deleting it
		// immediately; the poll loop reaps marked clients at the END of the
		// iteration. This is what makes "QUIT\r\nPRIVMSG #c :hi\r\n" arriving
		// in one packet safe: the dispatch loop sees the mark, stops feeding
		// this client more lines, and no handler is left holding a dangling
		// reference. See docs/ARCHITECTURE.md section 4.
		bool				isDisconnecting() const;
		void				markDisconnecting(const std::string &reason);
		const std::string	&getQuitReason() const;

	private:
		friend class Server;

		// Only Server may see the fd. Handlers cannot call this. 
		//getFd might not be needed
		//Server::_clients is std::map<int, Client *> — keyed by fd. 
		//Every place Server needs an fd, it's iterating that map and already holds the key: 
		//reapDisconnected closes it->first, the pollfd array is built from it->first, 
		//handleReadable(int fd) receives it as a parameter. disconnectClient only sets a flag, 
		//and sendToClient only calls queueOutput — neither touches an fd at all.
		int	getFd() const;

		int			_fd;
		std::string	_nickname;
		std::string	_username;
		std::string	_realname;
		std::string	_hostname;
		bool		_hasPass;
		bool		_hasNick;
		bool		_hasUser;
		bool		_welcomeSent;
		bool		_disconnecting;
		std::string	_quitReason;
		std::string	_readBuffer;
		std::string	_outputBuffer;
};

#endif
