#include <string>

#include "Client.hpp"
#include "Limits.hpp"
#include "Server.hpp"

#include "harness.hpp"

// Unit tests for the transport track's write path.
//
// NO SOCKET IS OPENED HERE, and that is not a limitation of the tests — it is
// the design paying off. Server's constructor deliberately touches nothing:
// the listening socket is created by run(), which these tests never call. And
// sendToClient does not write to an fd either; it truncates, appends CRLF and
// hands the result to Client::queueOutput. So the whole outbound contract —
// the 510-byte limit, the CRLF, the SendQ ceiling, the deferred disconnect —
// is testable with two objects on the stack.
//
// The fd of -1 is a deliberate booby trap: if any of this ever starts calling
// send() behind the seam, the test suite will notice instead of the evaluator.

static void	testOutgoingTruncation(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	server.sendToClient(client, "PING :token");
	checkEqual(client.getOutputBuffer(), "PING :token\r\n",
		"sendToClient appends CRLF and nothing else");

	Client	longLine(-1, "localhost");

	server.sendToClient(longLine, std::string(600, 'a'));
	check(longLine.getOutputBuffer().size() == irc::MAX_PAYLOAD_LEN + 2,
		"a 600-byte line goes out as 510 + CRLF");
	checkEqual(longLine.getOutputBuffer().substr(irc::MAX_PAYLOAD_LEN),
		"\r\n",
		"the CRLF survives the truncation, and is not cut in half");

	// The boundary in both directions: 510 is legal and untouched, 511 is not.
	Client	exact(-1, "localhost");

	server.sendToClient(exact, std::string(irc::MAX_PAYLOAD_LEN, 'b'));
	check(exact.getOutputBuffer().size() == irc::MAX_PAYLOAD_LEN + 2,
		"exactly 510 bytes is not truncated");

	Client	overByOne(-1, "localhost");

	server.sendToClient(overByOne, std::string(irc::MAX_PAYLOAD_LEN + 1, 'c'));
	check(overByOne.getOutputBuffer().size() == irc::MAX_PAYLOAD_LEN + 2,
		"511 bytes loses exactly one");
}

static void	testSendQueueCeiling(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	// 200 * 512 = 102400, comfortably past MAX_OUTPUT_QUEUE (65536). This is a
	// client that stopped reading — Ctrl+Z in nc, or hostile — while traffic
	// kept arriving for it.
	for (int i = 0; i < 200; ++i)
		server.sendToClient(client, std::string(irc::MAX_PAYLOAD_LEN, 'x'));

	check(client.isDisconnecting(),
		"overflowing SendQ marks the client");
	checkEqual(client.getQuitReason(), "SendQ exceeded",
		"and records why, for the ERROR line and the log");
	check(client.getOutputBuffer().size() <= irc::MAX_OUTPUT_QUEUE,
		"the queue never grew past its ceiling");
}

static void	testDeferredDisconnect(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	server.disconnectClient(client, "Request too long");
	check(client.isDisconnecting(), "disconnectClient marks the client");
	checkEqual(client.getOutputBuffer(), "ERROR :Request too long\r\n",
		"and queues the ERROR the reap will flush");

	// The client is STILL A VALID OBJECT here. Nothing was deleted: that only
	// happens in reapDisconnected, at the end of a poll iteration, which is
	// what makes "QUIT :bye\r\nPRIVMSG #c :x\r\n" in one packet safe.
	checkEqual(client.getHostname(), "localhost",
		"the Client& stays valid after disconnectClient returns");

	// Second call is a no-op. Without that guard, a disconnect on a client
	// whose queue is already full recurses through sendToClient until the
	// stack runs out.
	server.disconnectClient(client, "some other reason");
	checkEqual(client.getQuitReason(), "Request too long",
		"a second disconnect does not overwrite the first reason");
	checkEqual(client.getOutputBuffer(), "ERROR :Request too long\r\n",
		"and does not queue a second ERROR");
}

static void	testDisconnectOnFullQueueTerminates(void)
{
	Server	server(6667, "secret");
	Client	client(-1, "localhost");

	// The recursion trap itself, end to end: fill the queue so that the ERROR
	// cannot be queued, then disconnect. Reaching the next line at all is the
	// assertion — infinite recursion would take the test binary down with it.
	for (int i = 0; i < 200; ++i)
		server.sendToClient(client, std::string(irc::MAX_PAYLOAD_LEN, 'x'));
	server.disconnectClient(client, "another reason");

	checkEqual(client.getQuitReason(), "SendQ exceeded",
		"disconnecting a client with a full queue terminates");
}

void	runServerTests(void)
{
	testOutgoingTruncation();
	testSendQueueCeiling();
	testDeferredDisconnect();
	testDisconnectOnFullQueueTerminates();
}
