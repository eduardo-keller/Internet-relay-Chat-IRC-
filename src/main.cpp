#include <cstdlib>
#include <iostream>
#include <string>

#include "Channel.hpp"
#include "Client.hpp"
#include "Command.hpp"
#include "Message.hpp"
#include "Replies.hpp"
#include "Server.hpp"
#include "Utils.hpp"

// Phase 0 skeleton: validates the command line and exits.
// Phase 2 replaces the body of main() with `Server server(port, password);
// server.run();`. Everything above is included so that `make` proves all
// contract headers still compile under -Wall -Wextra -Werror -std=c++98.

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

	std::cout << "ircserv: would listen on port " << port
		<< " (not implemented yet)" << std::endl;
	return (0);
}
