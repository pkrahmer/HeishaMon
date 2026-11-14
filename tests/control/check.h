/*
 * The checks every test program of the control code shares. Each check compares exactly,
 * prints one line with the file, the line and both values when it fails, and counts the
 * failure, so that a test program runs every check and ends with `return check_result();`.
 */
#ifndef CHECK_H
#define CHECK_H

#include <stdbool.h>
#include <stdint.h>

/* Passes when the condition holds; the text is the condition as written. */
void check_true(const char *file, int line, bool condition, const char *text);

/* Passes when both are equal as doubles, or both are NaN; -0.0 equals 0.0. No tolerance:
 * the core is compared bit for bit with the Python reference. */
void check_double(const char *file, int line, double actual, double expected);

/* Passes when both are equal. */
void check_uint(const char *file, int line, uint32_t actual, uint32_t expected);

/* Passes when strcmp finds both texts equal. */
void check_text(const char *file, int line, const char *actual, const char *expected);

/* Returns the number of failed checks since the start or the last check_reset. */
int check_failures(void);

/* Sets the number of failed checks to 0. */
void check_reset(void);

/* Returns 0 when no check failed, and 1 otherwise: the exit status of a test program. */
int check_result(void);

#define CHECK_TRUE(condition) check_true(__FILE__, __LINE__, (condition), #condition)
#define CHECK_DOUBLE(actual, expected) check_double(__FILE__, __LINE__, (actual), (expected))
#define CHECK_UINT(actual, expected) check_uint(__FILE__, __LINE__, (actual), (expected))
#define CHECK_TEXT(actual, expected) check_text(__FILE__, __LINE__, (actual), (expected))

#endif
