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
class Client
{
	public:
		Client();
		explicit Client(int fd, const std::string &hostname);
		Client(const Client &other);
		Client	&operator=(const Client &other);
		~Client();

		int					getFd() const;
		const std::string	&getNickname() const;
		const std::string	&getUsername() const;
		const std::string	&getRealname() const;
		const std::string	&getHostname() const;

		void	setNickname(const std::string &nickname);
		void	setUsername(const std::string &username);
		void	setRealname(const std::string &realname);

		// Registration state. registered == hasPass && hasNick && hasUser.
		bool	hasPass() const;
		bool	hasNick() const;
		bool	hasUser() const;
		bool	isRegistered() const;
		void	setHasPass(bool value);
		void	setRegistered(bool value);

		// "nick!user@host" — the source prefix of every message this client
		// originates. Used by JOIN/PART/PRIVMSG/KICK/QUIT echoes.
		std::string	prefix() const;

		// --- read side: pure logic, no recv() in here ---------------------
		// Transport appends whatever recv() returned, however fragmented.
		void	appendToReadBuffer(const std::string &data);
		// Pops one complete line (up to and including CRLF or a bare LF) and
		// returns it WITHOUT the line ending. Returns false when the buffer
		// holds no complete line yet, which is the partial-packet case.
		bool	extractCommand(std::string &out);

		// --- write side: pure logic, no send() in here ---------------------
		// Non-blocking send() may accept only part of a write, so outbound
		// data is queued here and drained by the poll loop under POLLOUT.
		void				queueOutput(const std::string &data);
		const std::string	&getOutputBuffer() const;
		void				consumeOutput(std::size_t bytes);
		bool				hasPendingOutput() const;

	private:
		int			_fd;
		std::string	_nickname;
		std::string	_username;
		std::string	_realname;
		std::string	_hostname;
		bool		_hasPass;
		bool		_registered;
		std::string	_readBuffer;
		std::string	_outputBuffer;
};

#endif
