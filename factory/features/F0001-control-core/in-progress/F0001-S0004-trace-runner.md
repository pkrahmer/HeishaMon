---
stage: approval
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 6
comments_read: []
handed_over: null
claim: null
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
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-11T20:33:28.280462Z'
  stage: doing
  run: 658ccb3fa0b74a31a022de384ff1a17b
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-11T20:33:36.518051Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-11T20:35:11.322140Z'
  stage: review
  run: 9d8e3ee744f84116b5eba75fe4c0a1bc
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-11T20:35:17.036705Z'
  stage: doing
- number: 7
  speaker: builder
  kind: result
  at: '2026-11-11T20:37:39.405882Z'
  stage: doing
  run: 8524548b0c784940a1f748a3a0e89780
- number: 8
  speaker: factory
  kind: started
  at: '2026-11-11T20:37:45.308395Z'
  stage: review
- number: 9
  speaker: reviewer
  kind: result
  at: '2026-11-11T20:38:40.668790Z'
  stage: review
  run: 122dced4d5104fac9171562a0e0071be
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
3. builder: Built the trace runner. All thirteen golden traces replay through the C core with 0 mismatches on the first run (332,239 calls). The traces showed no fault in the core, so no core file changed.

   Built (all new except the two docs):
   - `tests/control/trace.h`, `tests/control/trace.c`: a shared helper with the reader and replayer from sections 3 and 4 of the format. It provides `trace_replay` as the Interface gives it, plus `trace_replay_to(FILE *, …)`, which sends the mismatch lines to a stream of the caller's choice.
   - `tests/control/test_traces.c`: replays the thirteen traces from a table with their call counts and a limit of 10, and prints the `traces:` line. It then runs table tests over the faulty traces.
   - `tests/control/data/bad/`, 22 small traces:
     - valid: `valid_small` (12 calls)
     - comparison rules (criterion 6):
       - `match_nan` holds a NaN: readings of ±1e308 make the balance inf − inf, so `pnet`, `E` and the ledger are NaN.
       - `mismatch_nan` records `E=nan` where the core holds 0.0.
       - `match_negative_zero` records `-0.0` for E, dt, int and ledger terms where the core holds 0.0.
       - `match_pnet_none` records `pnet=none` at t 0, `nan` and `inf`.
       - `mismatch_pnet_none` records none where the decision has inputs.
       - `mismatch_pnet_number` records a number where it has none.
     - mismatches: `wrong_energy` (line 12, key E), `wrong_everywhere` (a wrong `int` on 6 lines; with a limit of 3, exactly 3 mismatches)
     - format errors, one file each: `version_2`, `unknown_config_key`, `invalid_config` (`hysteresis_wh 0.0`, refused at `begin`), `unknown_call`, `unknown_signal`, `unknown_demand`, `trailing_text` (`advance 10.0x`), `pair_order`, `missing_pair`, `wrong_end_count`, `no_end` (format_line one past the last line), `long_line` (1,025 characters)
     - boundary: `longest_line` (1,024 characters, accepted)
     - All expected values were filled in from replaying the files. A valid copy of each format-error file's base passes, so each one fails only on its fault.
   - `docs/control/core.md`: new last section, *How the core is proven*.
   - `docs/control/decisions.md`: four entries:
     - the traces showed no fault;
     - where the reader is strict where the format leaves room;
     - why `trace_replay_to` exists;
     - a file that cannot be opened gives false with format_line 0.

   Decisions where the story left room:
   - The helper has a second function, `trace_replay_to`. The test needs the first printed line, and C99 cannot redirect stdout without POSIX `dup2`. Rejected: `dup2` under `_POSIX_C_SOURCE`, and a global output setting.
   - Mismatch keys for the ledger are printed per term, for example `ledger.solar`, so the line shows which term differed.
   - A mismatch limit below 1 counts as 1.
   - The reader refuses:
     - anything other than a blank or comment line after `end`
     - `fallback_w.` of a required signal
     - a `clamped` other than 0 or 1
     - double spaces
     - `trim_limit_enabled` as a key (`trim_limit_k none` sets it)
   - `state_max_age_s` and `persist_interval_s` must still be numbers, though they are ignored.
   - Sets are accepted in any order and compared by their bit mask.
   - The 1,024-character buffer counts the line without its newline.
   - The whole line is parsed before the call is made, so a format error never follows a call.

   Checks:
   - Before trusting the zero, I corrupted a copy of `winter_day` (one demand) and of `edge_cases` (one E). Both were reported at the changed line.
   - The bad-trace tests failed before their files existed.
   - `make check` is green; `test: green (4.2 s)`.
   - Smoke 1: `traces: 13 files, 332239 calls, 0 mismatches` and `control: 7 test programs passed`.
   - Smoke 2: `CTL_TRACES is not set; run make test` and `exit 1`.

   Noticed, not touched:
   - In `match_nan`, a NaN energy passes `ctl_clamp` and sets `clamped=1` (`NaN != NaN`). Python's `clamp` does the same, but no golden trace covers it.
   - Section 2 of `golden-traces.md` describes the eleven library scenarios only as a group. The doc table's rows for them therefore stick to what the trace headers and section 2 say (scenario name, hours, parameter), not guessed behaviour.
   - Upstream's rules harness prints compiler warnings to stderr during `make test`. That is upstream code and was left alone.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Review, round 0. I read the code, not only the journal. The diff adds files only. No test file that `develop` already had is changed, renamed or deleted: the only changed files that are not new are `docs/control/core.md`, `docs/control/decisions.md` and the work item. Criteria 1–5 and 7 have tests, and those tests check what the criteria say. For criterion 6, `match_nan`, `mismatch_nan`, `match_negative_zero` and the pnet files cover the rule both ways. The 1,024-character limit is tested on both sides (`longest_line` line 7 has 1,024 characters, `long_line` line 7 has 1,025). The mismatch limit is pinned exactly (3 of 6). After the builder, `make check` was green. Architecture holds: only test files and docs changed, and the helper includes only the core's headers. The docs table matches section 2 and the trace headers ("15 % of true" is backed by `docs/control/algorithm-plan.md:311`).

   Findings:

   1. `tests/control/test_traces.c` (`test_comparison_mismatches`) and `tests/control/data/bad/`: the Interface says every pair of an `evaluate` line is compared, but only `E`, `int`, `pnet` and a NaN `E` have a failing-side test. A replayer that never compared `demand`, `reason`, `phase`, `dt`, `skip`, `clamped`, `unhealthy`, `substituted` or any ledger term would still pass every test. This is a gap in the story too: criterion 6 names only nan, -0.0 and pnet. Fix: add one faulty trace, built from `valid_small`, with exactly one wrong value for each of these keys (one per ledger term), each on its own line or with the count known. Replay it with a limit above that count, and check that `mismatches` equals the number of planted faults.
   2. `tests/control/test_traces.c` (`test_format_errors`) and `tests/control/data/bad/`: some format errors the Interface names have no row. These are a surplus field (for example a fifth state pair, or a second argument to `advance`) and an unknown enum name in the decision pairs (a `reason` or `phase` name). Some refusals that the builder decided and recorded in `docs/control/decisions.md` also have none: a non-comment line after `end`, a double space, `clamped=2`, and `fallback_w.T_INDOOR`. The same goes for the file that cannot be opened, which should give false with `format_line` 0. This is also a gap in the story, because criterion 5 lists none of these. Fix: add one small file and one table row for each, with its expected `format_line`; for the missing file, add a row with a path that does not exist and expect `format_line` 0.

   Notes:
   - `tests/control/trace.c` `parse_call_kind`: the loop bound `4` should be `sizeof NAMES / sizeof NAMES[0]`.
   - `tests/control/trace.c` `format_signal_set`: it fills a `TEXT_MAX` buffer with `strcat`. It is safe for five names, but the buffer is written without its size.
6. factory: started doing as heishamon.
7. builder: Round 1. I fixed both review findings and acted on both notes. No core file changed. `make check` is green (`test: green (4.2 s)`).

   Finding 1: every compared key now has a test that fails when the key is wrong.
   - New file `tests/control/data/bad/wrong_every_key.trace`. It is `valid_small` with 15 wrong values planted, one for each key that had no failing-side test:
     - line 7: `demand`, `reason`, `phase`, `unhealthy`
     - line 9: state `skip`
     - line 10: state `clamped`
     - line 16: `dt`, `skip`, `clamped`, `substituted` and each of the five ledger terms
     - `E`, `int` and `pnet` were already covered by other files.
   - New test `test_every_key_is_compared` in `tests/control/test_traces.c`. It replays the file with a limit of 25 and checks:
     - the result is false, with exactly 15 mismatches, `format_line` 0 and 12 calls;
     - the printed lines, read in order, name the planted line and key of each fault (`check_reported`).
   - I made sure it catches a skipped key: I built a copy of the replayer that skips `reason` and `ledger.trim`, and against it the test fails (wrong count, and the lines no longer match).

   Finding 2: new rows in `test_format_errors`, one file each, built on the same 2-call base as the existing format-error files:
   - `surplus_state_pair` (a fifth state pair), line 7
   - `surplus_argument` (`advance 10.0 20.0`), line 7
   - `unknown_reason` (`reason=SLEEPING`), line 7
   - `unknown_phase` (`phase=PAUSED`), line 7
   - `double_space`, line 7
   - `clamped_two`, line 7
   - `text_after_end` (a call line after `end`), line 9
   - `fallback_required` (`config fallback_w.T_INDOOR`), line 4
   - `does_not_exist.trace` (a missing file): expects false with `format_line` 0. That file is deliberately absent.

   The rule from round 0 still holds: a valid copy of each base passes, so each file fails only on its fault. I checked this for the base and for valid copies of `fallback_required`, `unknown_reason` and `unknown_phase`.

   Notes:
   - `tests/control/trace.c` `parse_call_kind`: the loop bound is now `sizeof NAMES / sizeof NAMES[0]`.
   - `format_signal_set` now takes the buffer's size and appends with `snprintf` through a small `append` helper instead of `strcat`.

   Docs: in `docs/control/decisions.md`, the entry on the strict reader now says that each refusal, and each compared key, has a faulty trace of its own. Nothing else changed.

   Smoke tests, both as expected:
   - 1 prints `traces: 13 files, 332239 calls, 0 mismatches` and `control: 7 test programs passed`.
   - 2 prints `CTL_TRACES is not set; run make test` and `exit 1`.

   Not touched: the gap in the story's criteria that the reviewer named. Criteria 5 and 6 still list fewer cases than the tests now cover; I did not rewrite the Description for this, since the tests go further than the criteria and contradict none of them.

   `check`: check: green
8. factory: started review as heishamon.
9. reviewer: Review, round 1. I read the code in `tests/control/trace.c`, `trace.h` and `test_traces.c`, and every new file under `tests/control/data/bad/`. I did not rely on the journal's account.

   Both findings from round 0 are fixed:
   - Finding 1: `wrong_every_key.trace` plants exactly 15 faults: 4 on line 7, 1 on line 9, 1 on line 10 and 9 on line 16. Every value the replay checks against is the core's own output and never one read from the trace, so one fault cannot cause another. `test_every_key_is_compared` pins the count at 15 and checks the line and key of each printed mismatch in order. Every compared key (demand, reason, phase, E, dt, int, skip, clamped, pnet, unhealthy, substituted, and the five ledger terms) now fails a test when it is wrong.
   - Finding 2: `test_format_errors` now has a row for each case:
     - a surplus pair
     - a surplus argument
     - an unknown reason
     - an unknown phase
     - a double space
     - `clamped=2`
     - text after `end`
     - `fallback_w.T_INDOOR`
     - a missing file, which expects `format_line` 0

     I checked each expected `format_line` against its file.

   Checklist:
   1. Acceptance criteria:
      - Criteria 1–7 each have a test that checks what the criterion says.
      - The 1,024-character line limit is tested on both sides (`longest_line` and `long_line`). I traced `next_line` for both cases, with and without a trailing newline.
      - The mismatch limit is pinned at exactly 3 of 6.
      - The journal reports `make check` green after the builder.
   2. Architecture: only new files under `tests/control/` and two docs under `docs/control/` changed. The helper includes only the core's headers. No upstream file was touched.
   3. Embedded safety: no code that runs on the board changed. The helper has no dynamic memory and no recursion, and every loop has a bound.
   4. Quality: both round-0 notes are addressed.
   5. Hygiene:
      - The journal records the main decision (`trace_replay_to`) and what was rejected (`dup2` and a global output setting).
      - `core.md` *How the core is proven* matches the code: exact `==` or both NaN, ten mismatches per golden trace, and the output line.
      - `decisions.md` records that the traces showed no fault.
   6. Earlier tests: the diff modifies no existing test file. The only non-added paths are `docs/control/core.md` and `docs/control/decisions.md`.

   no findings

   Notes:
   - `tests/control/trace.c` `format_number`: it writes with the implicit `TEXT_MAX` rather than a size the caller passes, unlike `format_signal_set`. Every caller's buffer has `TEXT_MAX` characters, so it is safe today.
   - `docs/control/decisions.md` "The trace reader is strict…": the refusal of `trim_limit_enabled` as a config key and of a non-number for `state_max_age_s`/`persist_interval_s` have no faulty trace of their own. `unknown_config_key` covers the general rule. The sentence "Each of these refusals has a faulty trace of its own" reads as if it included them.
   - Story: criteria 5 and 6 still list fewer cases than the tests now cover (named in round 0). The tests go beyond the criteria without contradicting them. The human may want to add the extra cases to the Description.
