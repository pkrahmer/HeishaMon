---
stage: doing
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: null
comments_read: []
handed_over: null
claim:
  instance: heishamon
  stage: doing
  started_at: '2026-11-11T20:25:59.301056Z'
  lease_duration: 60
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-11T20:25:59.295081Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-11T20:25:59.301056Z'
  stage: doing
---

# The trace runner over the thirteen golden traces

## Description

The control core gets its proof: a test program that replays every golden trace of the Python reference through the C core and compares every recorded value after every call, exactly. The owner needs it to know that the port decides as the reference does, on 332,239 calls that cover synthetic weather, measured weather, lost and late readings, sensor outages, masks and scripted edge cases.

The traces are `tests/control/data/*.trace.gz`. `make test` unpacks them and names their folder in the environment variable `CTL_TRACES`. `docs/control/golden-traces.md` is their format, and its section 4 says how a replayer works; this story follows it.

When a trace shows a difference, the core is wrong: the builder fixes the core in this story and names in its entry the module, the rule and the trace line that showed it. The builder never changes a trace, never adds a tolerance and never leaves a value out of the comparison. A difference the builder cannot explain from the Python source ends the stage with `question`.

The story adds no function to the core's interface.

## Interface

New files: `tests/control/trace.h` and `tests/control/trace.c` (the reader and the replayer, a shared helper), `tests/control/test_traces.c`, and the small faulty traces under `tests/control/data/bad/`. The check helper is used as it is.

```c
/* tests/control/trace.h */
typedef struct {
    long calls;        /* call lines replayed */
    long mismatches;   /* values that differed, counted up to the limit */
    long format_line;  /* the line of the first format error, or 0 */
} trace_result;

bool trace_replay(const char *path, long mismatch_limit, trace_result *result);
```

`trace_replay` reads the trace at `path`, replays it from `ctl_state_init`, and returns true only when the file was read to its `end` line without a format error and without a mismatch. It fills `result` in every case.

The reader, by section 3 of the format:

- The first line is exactly `heatnrgx-golden 1`. Blank lines and lines that start with `#` are skipped everywhere.
- `scenario <name>` is read and not used.
- `config <key> <value>` sets the field of a `ctl_config` that starts from `ctl_config_defaults`. The keys are the fields of the C configuration, with `max_age_s.<SIGNAL>` for each of the five signals and `fallback_w.<SIGNAL>` for the two optional ones. `trim_limit_k none` sets `trim_limit_enabled` to false; a number sets it to true. The keys `state_max_age_s` and `persist_interval_s` are read and ignored. Any other key is a format error.
- `begin` ends the header. The configuration must then be valid by `ctl_config_validate`; an invalid one is a format error at the line of `begin`.
- Each body line is a call, ` | `, and the pairs `key=value` in the fixed order of the format. The calls are `observe <t> <SIGNAL> <value>`, `advance <t>`, `allow <t> <DEMANDS>` and `evaluate <t>`. A set is names separated by commas, or `-` for the empty set.
- `end <n>` closes the body; `n` must equal the number of call lines read. A file that ends without it, or with another count, is a format error.
- Anything else is a format error: an unknown first word, an unknown enum name, a missing or surplus field, a pair with another key or in another place than the format gives, a number `strtod` does not consume to its end, and a line longer than the reader's buffer of 1,024 characters.
- A format error stops the replay of that file. `format_line` is its line number, counted from 1 and including blank and comment lines.

The replay and the comparison:

- `observe` calls `ctl_observe`, `advance` calls `ctl_advance`, `allow` calls `ctl_set_allowed` with the demands as written, and `evaluate` calls `ctl_evaluate`.
- After `observe`, `advance` and `allow`, the pairs are compared with the state: `E` with `energy_wh`, `int` with `integrated_s`, `skip` with `skipped_s`, `clamped` with `clamped`.
- After `evaluate`, the pairs are compared with the decision: `demand`, `reason`, `phase`, `E`, `dt`, `int`, `skip`, `clamped`, `pnet` (the balance's `p_net_w`, where `none` matches `has_inputs` false), `unhealthy`, `substituted`, and `ledger` (the five terms in the order produced, solar, internal, transmission, trim).
- Numbers are compared as doubles and exactly: equal by `==`, or both NaN. So `-0.0` equals `0.0`. Enums are compared by value and sets by their bit mask.
- Each value that differs prints one line to standard output with the file's name, the line number, the key, the expected value and the actual value, numbers with 17 significant digits, and counts as one mismatch. After `mismatch_limit` mismatches the replay of that file stops.

`tests/control/test_traces.c`:

- It reads the folder from `CTL_TRACES`. Without the variable it prints `CTL_TRACES is not set; run make test` and fails.
- It replays the thirteen traces by their names, from a table in the test: `barbarastr_march`, `cold_snap_undersized`, `dhw_interruptions`, `edge_cases`, `flaky_transport`, `indoor_sensor_outage`, `masked_cooling`, `model_mismatch`, `shoulder_season`, `steady_winter`, `summer_day`, `underreported_solar`, `winter_day`, each as `<name>.trace` with a mismatch limit of 10.
- When all thirteen pass it prints one line: `traces: 13 files, 332239 calls, 0 mismatches`, with the numbers it counted.
- It then replays each faulty trace under `tests/control/data/bad/` by its path from the root of the checkout and checks the result named in the criteria. The lines these replays print are expected; the test prints `expected mismatches and format errors follow` before them.

The faulty traces are small text files the builder writes, each a valid trace of a few calls with one fault. Their expected values come from replaying them, not from hand calculation.

## Acceptance criteria

1. Each of the thirteen golden traces replays with `trace_replay` returning true, `mismatches` 0, `format_line` 0, and `calls` equal to the number on the trace's `end` line. The counts are: `barbarastr_march` 129493, `cold_snap_undersized` 12952, `dhw_interruptions` 14033, `edge_cases` 3236, `flaky_transport` 7403, `indoor_sensor_outage` 9657, `masked_cooling` 25913, `model_mismatch` 12952, `shoulder_season` 25912, `steady_winter` 12952, `summer_day` 25912, `underreported_solar` 25912, `winter_day` 25912.
2. `tests/control/data/bad/valid_small.trace`, a correct trace of at least one call of each of the four kinds with a `config trim_limit_k none` line, a blank line and a comment line in its body, replays true with its number of calls.
3. A wrong value is found: `bad/wrong_energy.trace` is `valid_small.trace` with the `E` of one `advance` line changed in its last digit. Its replay returns false with `format_line` 0 and at least 1 mismatch, and the first line it prints names that line number and the key `E`.
4. The mismatch limit holds: `bad/wrong_everywhere.trace` has a wrong `int` on at least five call lines. With a limit of 3, its replay returns false with `mismatches` exactly 3.
5. Each format error is refused, as rows of one table test over one file each; the replay returns false and `format_line` is the line of the fault: a first line `heatnrgx-golden 2`; an unknown configuration key; a configuration that is invalid at `begin` (`config hysteresis_wh 0.0`); an unknown first word in the body; an unknown signal name in an `observe` line; an unknown demand name in an `allow` line; a number with trailing text (`advance 10.0x`); a state pair in the wrong order (`int=` before `E=`); a missing pair on an `evaluate` line; a wrong count on the `end` line; a file that ends without an `end` line, where `format_line` is one past the last line; and a line of more than 1,024 characters.
6. The comparison rules, each shown by a faulty or a valid small trace: a recorded `nan` matches a NaN the core holds or the trace is refused as a mismatch when the core holds a number; a recorded `-0.0` matches `0.0`; `pnet=none` matches a decision without inputs and mismatches one with inputs. The builder picks calls that produce these states (a NaN `t` in `evaluate` gives `pnet=none`; `edge_cases.trace` shows such lines) and names the files in its entry.
7. Without `CTL_TRACES` in the environment, `test_traces` prints `CTL_TRACES is not set; run make test` and exits with 1. The smoke test checks this.
8. `make check` stays green, and `make test` takes less than 60 seconds on the stage machine.
9. docs: `docs/control/core.md` gets a last section, *How the core is proven*: what a golden trace is in two sentences with a pointer to `golden-traces.md`, what the runner compares and that it compares exactly, the thirteen traces as a table (name, calls, what it is there for, taken from section 2 of `golden-traces.md`), how to run the runner and what its line of output means, and what to do when a trace differs: fix the core, never the trace; when the Python core changes, the traces are exported again in the Python project and replaced here as a whole. `docs/control/decisions.md` records every fault in the core that the traces showed, each with the trace line that showed it, or says that they showed none.

## Smoke test

```sh
make test | grep -E '^(traces|control):'
```

Expect: two lines, `traces: 13 files, 332239 calls, 0 mismatches` and `control: 7 test programs passed`

```sh
d=$(mktemp -d); trap 'rm -rf "$d"' EXIT; gcc -std=c99 -ffp-contract=off -IHeishaMon/src/control -Itests/control tests/control/test_traces.c tests/control/trace.c tests/control/check.c $(find HeishaMon/src/control -name '*.c') -lm -o "$d/t"; env -u CTL_TRACES "$d/t"; echo "exit $?"
```

Expect: two lines, `CTL_TRACES is not set; run make test` and `exit 1`

## Journal

1. 2026-11-11 human: created.
2. factory: started doing as heishamon.
