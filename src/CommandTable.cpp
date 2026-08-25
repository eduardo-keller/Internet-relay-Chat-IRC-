#include "Command.hpp"

// The command table, and the whole dispatch mechanism: a map from an
// UPPERCASED command name to a function pointer. No command class hierarchy,
// no factory, no registry singleton — Server::handleLine uppercases what the
// client sent, looks it up here, and calls what it finds.
//
// THIS FILE IS OWNED BY THE TRANSPORT TRACK and it is the one place where the
// two tracks' work is wired together. That matters because buildCommandTable
// is a single function that both of us need to fill: if the domain session
// writes its own in src/Command.cpp, the linker sees two definitions and
// NEITHER binary builds. So the domain entries live below, in a marked block,
// one per line — see docs/FASE2.md section 3.3, incompatibility 1.

CommandTable	buildCommandTable()
{
	CommandTable	table;

	// --- TRANSPORT (Eduardo) ------------------------------------------
	table["PASS"] = &cmdPass;
	table["NICK"] = &cmdNick;
	table["USER"] = &cmdUser;
	table["QUIT"] = &cmdQuit;
	table["PING"] = &cmdPing;
	table["PONG"] = &cmdPong;
	table["CAP"] = &cmdCap;
	// Silent, like CAP END, and for the same reason: irssi sends both by
	// itself once a channel has two people in it. See step 1.5 of FASE3.md.
	table["WHO"] = &cmdWho;
	table["WHOIS"] = &cmdWhois;

	// --- DOMAIN (colega) ----------------------------------------------
	// Uncomment each line as its handler gains a body in the domain track's
	// source file. Registering one before the body exists is an undefined
	// reference that stops BOTH binaries, including make test, so these are
	// enabled one at a time and never speculatively.
	//
	table["JOIN"] = &cmdJoin;
	table["PART"] = &cmdPart;
	table["PRIVMSG"] = &cmdPrivmsg;
	// table["KICK"] = &cmdKick;
	// table["INVITE"] = &cmdInvite;
	table["TOPIC"] = &cmdTopic;

	// MODE is registered ahead of the other six, out of the order the TASKS.md
	// list suggests, and step 0.6 of docs/FASE3.md says why: irssi sends
	// "MODE <nick> +i" by itself right after registering, so without a handler
	// every connection collects a 421 in its status window. The body answers
	// the query and changes nothing yet.
	table["MODE"] = &cmdMode;

	return (table);
}
