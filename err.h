#pragma once

// Displays information about an erroneous termination of system functions
// and returns.
extern void syserr(const char *fmt, ...);

// Prints error information and returns.
__attribute__((unused)) extern void fatal(const char *fmt, ...);
