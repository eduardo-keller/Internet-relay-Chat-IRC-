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

// --- channels and messaging: DOMAIN track --------------------------------
void	cmdJoin(Server &server, Client &sender, const Message &msg);
void	cmdPart(Server &server, Client &sender, const Message &msg);
void	cmdPrivmsg(Server &server, Client &sender, const Message &msg);
void	cmdKick(Server &server, Client &sender, const Message &msg);
void	cmdInvite(Server &server, Client &sender, const Message &msg);
void	cmdTopic(Server &server, Client &sender, const Message &msg);
void	cmdMode(Server &server, Client &sender, const Message &msg);

#endif
