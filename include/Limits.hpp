#ifndef LIMITS_HPP
#define LIMITS_HPP

#include <cstddef>

// Protocol and resource limits. Both tracks need these, so they are decided
// once here rather than as magic numbers at each call site.
//
// The message limit is a protocol invariant from RFC 2812 section 2.3:
// "these messages SHALL NOT exceed 512 characters in length, counting all
// characters including the trailing CR-LF. Thus, there are 510 characters
// maximum allowed for the command and its parameters."
//
// The buffer limits are NOT protocol — they exist because the subject requires
// that the server never crash "even when it runs out of memory". An unbounded
// read buffer or output queue is a memory-exhaustion bug reachable by one
// well-behaved-looking client. See docs/ARCHITECTURE.md section 11.
namespace irc
{
	// Whole message, CRLF included.
	const std::size_t	MAX_MESSAGE_LEN = 512;
	// Everything before the CRLF. What sendToClient truncates to.
	const std::size_t	MAX_PAYLOAD_LEN = 510;

	// Cap on unparsed input held for one client. Exceeding this WITH no
	// complete line in the buffer means a single unterminated flood, not
	// legitimate pipelining: disconnect.
	const std::size_t	MAX_READ_BUFFER = 4096;

	// Cap on undelivered output queued for one client (what other servers
	// call SendQ). Exceeding it means the client has stopped reading while
	// traffic keeps arriving: disconnect rather than grow without bound.
	const std::size_t	MAX_OUTPUT_QUEUE = 65536;

	// How much we ask recv() for per readiness event.
	const std::size_t	RECV_CHUNK = 4096;

	// Name bounds. RFC 2812 section 2.3.1 allows only 9 characters for a
	// nickname, but that is a 1988 figure: every deployed server raised it,
	// and irssi fills the nickname from the system username, so a 9-character
	// cap turns away real users before they have typed anything. 30 is the
	// common modern value. Some bound is mandatory either way — the nickname
	// goes into ":nick!user@host" on every message the client originates, and
	// that prefix is charged against MAX_PAYLOAD_LEN.
	//
	// The channel limit is the RFC's own 50, unchanged.
	const std::size_t	MAX_NICKNAME_LEN = 30;
	const std::size_t	MAX_CHANNEL_LEN = 50;
}

#endif
