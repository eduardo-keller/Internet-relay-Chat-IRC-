#include <iostream>
#include <string>

// Minimal harness for the inner testing loop. No framework, no dependency:
// a counter, an assertion, and a summary. Every Phase 1 unit gets its tests
// added here (or in a sibling tests/*.cpp file, which the Makefile picks up).
//
// Reminder: these tests prove our logic is INTERNALLY CONSISTENT. They do not
// prove protocol conformance — only irssi and the RFC do that.

static int	g_passed = 0;
static int	g_failed = 0;

void	check(bool condition, const std::string &name)
{
	if (condition)
	{
		++g_passed;
		std::cout << "  ok   " << name << std::endl;
	}
	else
	{
		++g_failed;
		std::cout << "  FAIL " << name << std::endl;
	}
}

void	checkEqual(const std::string &actual, const std::string &expected,
			const std::string &name)
{
	if (actual == expected)
	{
		++g_passed;
		std::cout << "  ok   " << name << std::endl;
	}
	else
	{
		++g_failed;
		std::cout << "  FAIL " << name << std::endl;
		std::cout << "       expected [" << expected << "]" << std::endl;
		std::cout << "       actual   [" << actual << "]" << std::endl;
	}
}

int	main(void)
{
	std::cout << "running unit tests" << std::endl;

	// Placeholder so Phase 0 has something green to run. Delete it as soon
	// as the first real parser test lands.
	check(true, "harness works");

	std::cout << std::endl
		<< g_passed << " passed, " << g_failed << " failed" << std::endl;
	return (g_failed == 0 ? 0 : 1);
}
