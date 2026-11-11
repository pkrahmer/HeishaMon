#include "check.h"
#include "trace.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GOLDEN_MISMATCH_LIMIT 10
#define PATH_MAX_LENGTH 4096
#define BAD "tests/control/data/bad/"

typedef struct {
    const char *name;
    long calls;
} golden_row;

/* The calls each trace announces on its end line. */
static const golden_row GOLDEN[] = {
    {"barbarastr_march", 129493},
    {"cold_snap_undersized", 12952},
    {"dhw_interruptions", 14033},
    {"edge_cases", 3236},
    {"flaky_transport", 7403},
    {"indoor_sensor_outage", 9657},
    {"masked_cooling", 25913},
    {"model_mismatch", 12952},
    {"shoulder_season", 25912},
    {"steady_winter", 12952},
    {"summer_day", 25912},
    {"underreported_solar", 25912},
    {"winter_day", 25912},
};

static void test_golden_traces(const char *folder)
{
    const size_t count = sizeof GOLDEN / sizeof GOLDEN[0];
    const int failures_before = check_failures();
    long calls = 0;
    long mismatches = 0;
    size_t i;
    for (i = 0; i < count; i++) {
        char path[PATH_MAX_LENGTH];
        trace_result result;
        bool passed;
        snprintf(path, sizeof path, "%s/%s.trace", folder, GOLDEN[i].name);
        passed = trace_replay(path, GOLDEN_MISMATCH_LIMIT, &result);
        CHECK_TRUE(passed);
        CHECK_TRUE(result.mismatches == 0);
        CHECK_TRUE(result.format_line == 0);
        CHECK_TRUE(result.calls == GOLDEN[i].calls);
        calls += result.calls;
        mismatches += result.mismatches;
    }
    if (check_failures() == failures_before) {
        printf("traces: %lu files, %ld calls, %ld mismatches\n", (unsigned long)count, calls,
               mismatches);
    }
}

typedef struct {
    const char *path;
    long calls;
} valid_row;

static void test_valid_traces(void)
{
    const valid_row rows[] = {
        {BAD "valid_small.trace", 12},
        {BAD "match_nan.trace", 9},
        {BAD "match_negative_zero.trace", 3},
        {BAD "match_pnet_none.trace", 6},
        {BAD "longest_line.trace", 2},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        trace_result result;
        CHECK_TRUE(trace_replay(rows[i].path, 10, &result));
        CHECK_TRUE(result.calls == rows[i].calls);
        CHECK_TRUE(result.mismatches == 0);
        CHECK_TRUE(result.format_line == 0);
    }
}

/* The first line the replay printed, without its newline. */
static void first_printed_line(FILE *output, char *line, size_t size)
{
    line[0] = '\0';
    rewind(output);
    if (fgets(line, (int)size, output) != NULL) {
        line[strcspn(line, "\n")] = '\0';
    }
}

static void test_wrong_energy_is_found(void)
{
    char line[512];
    trace_result result;
    FILE *output = tmpfile();
    CHECK_TRUE(output != NULL);
    if (output == NULL) {
        return;
    }
    CHECK_TRUE(!trace_replay_to(output, BAD "wrong_energy.trace", 10, &result));
    CHECK_TRUE(result.format_line == 0);
    CHECK_TRUE(result.mismatches >= 1);
    first_printed_line(output, line, sizeof line);
    fclose(output);
    printf("%s\n", line);
    CHECK_TRUE(strncmp(line, BAD "wrong_energy.trace:12: E expected ",
                       strlen(BAD "wrong_energy.trace:12: E expected ")) == 0);
}

static void test_mismatch_limit_holds(void)
{
    trace_result result;
    CHECK_TRUE(!trace_replay(BAD "wrong_everywhere.trace", 3, &result));
    CHECK_TRUE(result.mismatches == 3);
    CHECK_TRUE(result.format_line == 0);
}

typedef struct {
    long line;
    const char *key;
} planted_fault;

/* Reads the next printed line and checks that it reports the fault: file, line and key. */
static void check_reported(FILE *output, const char *path, const planted_fault *fault)
{
    char expected[512];
    char line[512];
    snprintf(expected, sizeof expected, "%s:%ld: %s expected ", path, fault->line, fault->key);
    if (fgets(line, (int)sizeof line, output) == NULL) {
        line[0] = '\0';
    }
    line[strcspn(line, "\n")] = '\0';
    if (strncmp(line, expected, strlen(expected)) != 0) {
        printf("reported \"%s\", expected a line starting \"%s\"\n", line, expected);
    }
    CHECK_TRUE(strncmp(line, expected, strlen(expected)) == 0);
}

/* Every key the replayer compares but E, int and pnet, which other files cover, has one wrong
 * value; each must be reported, in the order of the file and of the pairs. */
static void test_every_key_is_compared(void)
{
    static const char path[] = BAD "wrong_every_key.trace";
    static const planted_fault faults[] = {
        {7, "demand"},
        {7, "reason"},
        {7, "phase"},
        {7, "unhealthy"},
        {9, "skip"},
        {10, "clamped"},
        {16, "dt"},
        {16, "skip"},
        {16, "clamped"},
        {16, "substituted"},
        {16, "ledger.produced"},
        {16, "ledger.solar"},
        {16, "ledger.internal"},
        {16, "ledger.transmission"},
        {16, "ledger.trim"},
    };
    const long count = (long)(sizeof faults / sizeof faults[0]);
    trace_result result;
    long i;
    FILE *output = tmpfile();
    CHECK_TRUE(output != NULL);
    if (output == NULL) {
        return;
    }
    CHECK_TRUE(!trace_replay_to(output, path, count + 10, &result));
    CHECK_TRUE(result.mismatches == count);
    CHECK_TRUE(result.format_line == 0);
    CHECK_TRUE(result.calls == 12);
    rewind(output);
    for (i = 0; i < count; i++) {
        check_reported(output, path, &faults[i]);
    }
    fclose(output);
}

typedef struct {
    const char *path;
    long mismatches;
} mismatch_row;

/* Each file is a valid trace but for one recorded value the core does not hold. */
static void test_comparison_mismatches(void)
{
    const mismatch_row rows[] = {
        {BAD "mismatch_nan.trace", 1},
        {BAD "mismatch_pnet_none.trace", 1},
        {BAD "mismatch_pnet_number.trace", 1},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        trace_result result;
        CHECK_TRUE(!trace_replay(rows[i].path, 10, &result));
        CHECK_TRUE(result.mismatches == rows[i].mismatches);
        CHECK_TRUE(result.format_line == 0);
    }
}

typedef struct {
    const char *path;
    long format_line;
} format_row;

static void test_format_errors(void)
{
    const format_row rows[] = {
        {BAD "version_2.trace", 1},
        {BAD "unknown_config_key.trace", 4},
        {BAD "invalid_config.trace", 5},
        {BAD "unknown_call.trace", 7},
        {BAD "unknown_signal.trace", 7},
        {BAD "unknown_demand.trace", 7},
        {BAD "trailing_text.trace", 7},
        {BAD "pair_order.trace", 7},
        {BAD "missing_pair.trace", 7},
        {BAD "wrong_end_count.trace", 8},
        {BAD "no_end.trace", 8},
        {BAD "long_line.trace", 7},
        {BAD "surplus_state_pair.trace", 7},
        {BAD "surplus_argument.trace", 7},
        {BAD "unknown_reason.trace", 7},
        {BAD "unknown_phase.trace", 7},
        {BAD "double_space.trace", 7},
        {BAD "clamped_two.trace", 7},
        {BAD "text_after_end.trace", 9},
        {BAD "fallback_required.trace", 4},
        /* A file that cannot be opened has no line at fault. */
        {BAD "does_not_exist.trace", 0},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        trace_result result;
        CHECK_TRUE(!trace_replay(rows[i].path, 10, &result));
        if (result.format_line != rows[i].format_line) {
            printf("%s: format_line %ld, expected %ld\n", rows[i].path, result.format_line,
                   rows[i].format_line);
        }
        CHECK_TRUE(result.format_line == rows[i].format_line);
        CHECK_TRUE(result.mismatches == 0);
    }
}

int main(void)
{
    const char *folder = getenv("CTL_TRACES");
    if (folder == NULL) {
        printf("CTL_TRACES is not set; run make test\n");
        return 1;
    }
    test_golden_traces(folder);
    test_valid_traces();
    printf("expected mismatches and format errors follow\n");
    test_wrong_energy_is_found();
    test_mismatch_limit_holds();
    test_every_key_is_compared();
    test_comparison_mismatches();
    test_format_errors();
    return check_result();
}
