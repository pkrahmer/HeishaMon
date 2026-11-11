#include "check.h"

#include <stdlib.h>

/* The replay script and the host twin talk through processes and pipes, which a C test cannot
 * reach without POSIX; their tests are a Python program beside them, run here so that every
 * test runs with the others. It prints its own failed checks. */
int main(void)
{
    int status = system("python3 -B tests/control/device/test_replay.py");
    CHECK_TRUE(status == 0);
    return check_result();
}
