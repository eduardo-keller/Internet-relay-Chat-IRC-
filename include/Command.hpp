#ifndef COMMAND_HPP
#define COMMAND_HPP

#include <map>
#include <string>

#include "Message.hpp"

class Client;
class Server;

// THE HANDLER SIGNATURE — one agreed shape for every command.
//
//     (server, sender, parsed message) -> effect
//
// A handler reads state, decides, mutates the domain, and emits replies
// through Server. It returns nothing: the effect IS the return value.
//
// Dispatch is a plain std::map from uppercased command name to function
// pointer. No command-object hierarchy, no factory, no registry singleton —
// a map lookup is the whole mechanism.
typedef void	(*CommandHandler)(Server &server, Client &sender,
					const Message &msg);

typedef std::map<std::string, CommandHandler>	CommandTable;

// Built once at startup. Keys are UPPERCASE; the dispatcher uppercases the
// incoming command before looking it up, since IRC commands are
// case-insensitive.
CommandTable	buildCommandTable();

// --- registration: TRANSPORT track ---------------------------------------
void	cmdPass(Server &server, Client &sender, const Message &msg);
void	cmdNick(Server &server, Client &sender, const Message &msg);
void	cmdUser(Server &server, Client &sender, const Message &msg);
void	cmdQuit(Server &server, Client &sender, const Message &msg);
void	cmdPing(Server &server, Client &sender, const Message &msg);
void	cmdPong(Server &server, Client &sender, const Message &msg);
// CAP is not in the subject's command list and we implement no capabilities,
// but irssi sends "CAP LS 302" before PASS on every connect. Answering 421
// puts an error in its status window, and the subject requires the reference
// client to connect "without encountering any error" — so this is a handler
// that deliberately does nothing. Decision D2 in docs/FASE2.md.
void	cmdCap(Server &server, Client &sender, const Message &msg);
// WHO and WHOIS are not in the subject's command list either, and this server
// has no user database to answer them from. They exist for the same reason CAP
// does: irssi sends both ON ITS OWN as soon as a second person is in a channel,
// and 421 in its status window is an error the subject forbids. Both are
// deliberately silent. Step 1.5 in docs/FASE3.md.
void	cmdWho(Server &server, Client &sender, const Message &msg);
void	cmdWhois(Server &server, Client &sender, const Message &msg);

// --- channels and messaging: DOMAIN track --------------------------------
void	cmdJoin(Server &server, Client &sender, const Message &msg);
void	cmdPart(Server &server, Client &sender, const Message &msg);
void	cmdPrivmsg(Server &server, Client &sender, const Message &msg);
void	cmdKick(Server &server, Client &sender, const Message &msg);
void	cmdInvite(Server &server, Client &sender, const Message &msg);
void	cmdTopic(Server &server, Client &sender, const Message &msg);
void	cmdMode(Server &server, Client &sender, const Message &msg);

#endif
