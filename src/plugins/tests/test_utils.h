#include "common/showmsg.h"

#define TEST(name, function, ...) do { \
	ShowMessage("-------------------------------------------------------------------------------\n"); \
	ShowNotice("Testing %s...\n", (name)); \
	if (!(function)(##__VA_ARGS__)) { \
		ShowError("Failed.\n"); \
	} else { \
		ShowStatus("Passed.\n"); \
	} \
	ShowMessage("-------------------------------------------------------------------------------\n"); \
} while (false)

#define context(message, ...) do { \
	ShowNotice("\n"); \
	ShowNotice("> " message "\n", ##__VA_ARGS__); \
} while (false)

#define expect(message, actual, expected, ...) do { \
	ShowNotice("\t" message "... ", ##__VA_ARGS__); \
	if (actual != expected) { \
		passed = false; \
		ShowMessage("" CL_RED "Failed" CL_RESET "\n"); \
		ShowNotice("\t\t(Expected: " CL_GREEN " %d " CL_RESET ", Received: " CL_RED " %d " CL_RESET ")\n", expected, actual); \
	} else { \
		ShowMessage("" CL_GREEN "Passed" CL_RESET "\n"); \
	} \
} while (false)
