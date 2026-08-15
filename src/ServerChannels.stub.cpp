#include <cstddef>

#include "Channel.hpp"
#include "Server.hpp"

// TEMPORARY — delete this file when src/Channel.cpp lands.
// See docs/FASE2.md sections 3.2 and 9 (step 9 replaces it with the real
// src/ServerChannels.cpp in the same commit that removes this one).
//
// WHY IT EXISTS. The Makefile globs src/*.cpp and links every object into both
// binaries. A single call to Channel::getMembers() with no Channel.cpp behind
// it is an undefined reference that stops the build — including `make test`.
// Collecting every Channel-touching body in ONE file means the transport track
// keeps building while the domain track has not shipped its model yet, and the
// hole is a file you can see rather than TODO comments scattered across three
// functions.
//
// WHY IT IS CORRECT, not merely a placeholder. _channels is provably empty
// throughout phase 2: the only thing that could ever insert into it is
// getOrCreateChannel, and that returns NULL here. No JOIN handler is
// registered either, so nothing can even ask. These no-ops therefore give the
// same observable behaviour as the real implementations would on an empty
// channel map.
//
// Channel.hpp is included although nothing here needs the class definition —
// Server.hpp already forward-declares it, and returning Channel* or taking
// Channel& works fine on an incomplete type. It stays because it keeps the
// header inside my compile check: a syntax error in Channel.hpp then breaks my
// build instead of surprising the domain track.
//
// Parameters are left unnamed on purpose: -Wextra turns on -Wunused-parameter,
// and (void)x seven times over would be worse to read.

Channel	*Server::findChannel(const std::string &)
{
	return (NULL);
}

Channel	*Server::getOrCreateChannel(const std::string &)
{
	return (NULL);
}

void	Server::removeChannel(const std::string &)
{
}

void	Server::broadcastToChannel(Channel &, const std::string &, const Client *)
{
}

void	Server::broadcastToPeers(const Client &, const std::string &, bool)
{
}

void	Server::sweepChannels(Client &)
{
}

void	Server::clearAllChannels()
{
}
