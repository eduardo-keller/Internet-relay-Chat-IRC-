#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <vector>

// SHARED MIDDLE: pure helpers, owned by neither track. Either dev implements
// these when blocked on their own track. No fds, no sockets, no globals.
namespace utils
{
	// IRC casemapping (RFC 2812 section 2.2): {}|^ are the lowercase of []\~.
	// Nicknames and channel names compare case-insensitively under this rule,
	// so plain std::tolower is NOT sufficient.
	std::string	toIrcLower(const std::string &s);
	bool		equalsIgnoreCase(const std::string &a, const std::string &b);

	// Splits on a single character. Empty fields are preserved, because
	// "JOIN #a,,#b" must not silently become two channels.
	std::vector<std::string>	split(const std::string &s, char delim);

	// Validation. Return value is the whole answer: no error strings here,
	// the caller decides which numeric to send.
	bool	isValidNickname(const std::string &nickname);
	bool	isValidChannelName(const std::string &name);

	// Numeric conversion without C++11's std::to_string.
	std::string	toString(int value);
	bool		parseInt(const std::string &s, int &out);
}

#endif
