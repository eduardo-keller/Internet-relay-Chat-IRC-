#ifndef HARNESS_HPP
#define HARNESS_HPP

#include <string>

// Declarations for the two assertions DEFINED in tests/test_main.cpp.
//
// Each unit under test gets its own tests/test_*.cpp with a single
// run*Tests() entry point, called from main(). Splitting it that way means the
// two tracks never edit the same test file, so there is nothing to conflict on
// at merge time — and the Makefile globs tests/*.cpp, so a new file needs no
// Makefile change.

void	check(bool condition, const std::string &name);
void	checkEqual(const std::string &actual, const std::string &expected,
			const std::string &name);

// --- per-unit entry points; add one line when you add a test file ---------
void	runClientTests(void);

#endif
