#include "Client.hpp"
#include "Command.hpp"
#include "Message.hpp"
#include "Server.hpp"

// The transport track's command handlers.
//
// STEP 5 LEAVES EVERY BODY EMPTY, on purpose. The dispatcher's registration
// gate is the thing being built right now, and an empty handler is what lets
// that gate be tested on its own: a command that reaches its handler produces
// no output, so ANY reply observed in a test came from the dispatcher and
// nowhere else. Step 6 fills PASS, NICK and USER plus the 001-004 burst; step
// 7 fills QUIT, PING, PONG and CAP.
//
// Parameters are unnamed because -Wextra turns on -Wunused-parameter and
// -Werror turns that into a build failure.

void	cmdPass(Server &, Client &, const Message &)
{
}

void	cmdNick(Server &, Client &, const Message &)
{
}

void	cmdUser(Server &, Client &, const Message &)
{
}

void	cmdQuit(Server &, Client &, const Message &)
{
}

void	cmdPing(Server &, Client &, const Message &)
{
}

void	cmdPong(Server &, Client &, const Message &)
{
}

void	cmdCap(Server &, Client &, const Message &)
{
}
