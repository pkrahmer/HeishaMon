#include "check.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void fail(void)
{
    failures++;
}

void check_true(const char *file, int line, bool condition, const char *text)
{
    if (condition) {
        return;
    }
    printf("%s:%d: expected true: %s\n", file, line, text);
    fail();
}

void check_double(const char *file, int line, double actual, double expected)
{
    if (actual == expected || (isnan(actual) && isnan(expected))) {
        return;
    }
    /* 17 significant digits tell apart any two doubles. */
    printf("%s:%d: got %.17g, expected %.17g\n", file, line, actual, expected);
    fail();
}

void check_uint(const char *file, int line, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        return;
    }
    printf("%s:%d: got 0x%lx, expected 0x%lx\n", file, line, (unsigned long)actual,
           (unsigned long)expected);
    fail();
}

void check_text(const char *file, int line, const char *actual, const char *expected)
{
    if (strcmp(actual, expected) == 0) {
        return;
    }
    printf("%s:%d: got \"%s\", expected \"%s\"\n", file, line, actual, expected);
    fail();
}

int check_failures(void)
{
    return failures;
}

void check_reset(void)
{
    failures = 0;
}

int check_result(void)
{
    return failures == 0 ? 0 : 1;
}
