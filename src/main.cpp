#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "Server.hpp"

// ./ircserv <port> <password>
//
// Argument validation, then hand over to Server::run(), which owns everything
// from the listening socket onwards.

static bool	parsePort(const std::string &arg, int &port)
{
	if (arg.empty())
		return (false);
	for (std::string::size_type i = 0; i < arg.size(); ++i)
	{
		if (arg[i] < '0' || arg[i] > '9')
			return (false);
	}
	long	value = std::strtol(arg.c_str(), NULL, 10);
	if (value < 1024 || value > 65535)
		return (false);
	port = static_cast<int>(value);
	return (true);
}

int	main(int argc, char **argv)
{
	if (argc != 3)
	{
		std::cerr << "Usage: " << argv[0] << " <port> <password>" << std::endl;
		return (1);
	}

	int	port = 0;
	if (!parsePort(argv[1], port))
	{
		std::cerr << "Error: port must be a number between 1024 and 65535"
			<< std::endl;
		return (1);
	}

	std::string	password(argv[2]);
	if (password.empty())
	{
		std::cerr << "Error: password must not be empty" << std::endl;
		return (1);
	}

	// An uncaught exception calls std::terminate, and terminating IS "quitting
	// unexpectedly" — the subject's grade-zero rule. Catching here turns a
	// failed bind into a message and exit(1), and unwinding through this catch
	// still runs ~Server, so the fds are closed on the way out.
	try
	{
		Server	server(port, password);

		server.run();
	}
	catch (const std::exception &e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return (1);
	}
	return (0);
}
