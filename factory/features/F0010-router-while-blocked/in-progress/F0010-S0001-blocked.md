---
stage: approval
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: 24
comments_read: []
handed_over: approval
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-12T22:52:33.246736Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-12T22:52:33.252992Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-12T23:00:39.313623Z'
  stage: doing
  run: 528e05ae11c84856aa6dcff3e0a8e06c
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-12T23:00:46.255339Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-12T23:01:50.294048Z'
  stage: review
  run: 943484cd380c420e84a79aa1c9d50151
---

# Assumed landing and the reason while blocked

## Description

The glue sends the router's commands only when commands may go to the pump: `enabled` is 1, HeishaMon is not in listen-only mode, and its optional PCB emulation is on. Its confirmation is the emulated frame, and it counts only while that frame goes out. On a blocked board, for example the owner's spare D1 that shadows the house with commands disabled, the confirmation is therefore never known. The router repeats its first send and ends in `FAULT`, which is not a real fault.

This story makes the glue step the loop with `assume_landed` true while commands are blocked, and with the confirmation from the frame as before while they are allowed. It also adds the telemetry entry `outputs/blocked`, which names why commands are blocked.

## Interface

Changed files: `HeishaMon/src/control/glue/ctl_telemetry.h`, `ctl_telemetry.c`, `ctl_firmware.cpp`, and in pure C a new `HeishaMon/src/control/glue/ctl_gate.h`, `ctl_gate.c` with the test `tests/control/test_gate.c`. `tests/control/test_telemetry.c` changes by the owner's leave as criterion 3 says.

```c
/* ctl_gate.h */
typedef enum { CTL_GATE_OPEN, CTL_GATE_DISABLED, CTL_GATE_LISTEN_ONLY, CTL_GATE_NO_OPTIONAL_PCB } ctl_gate;

ctl_gate ctl_gate_state(bool enabled, bool listen_only, bool optional_pcb);
const char *ctl_gate_name(ctl_gate gate);
```

- `ctl_gate_state` gives the first that holds, in this order:
  - `DISABLED` when `enabled` is false;
  - `LISTEN_ONLY` when `listen_only` is true;
  - `NO_OPTIONAL_PCB` when `optional_pcb` is false;
  - otherwise `OPEN`.
- `ctl_gate_name` gives `-`, `disabled`, `listen_only` and `no_optional_pcb`, and `invalid` for a value that is no member.
- **Telemetry.** `ctl_telemetry_source` gains `ctl_gate gate`. The outputs group gains `outputs/blocked` as its last entry, with `ctl_gate_name` of the source's gate, so the outputs count becomes 4.
- **The glue:**
  - it computes the gate with `ctl_gate_state` wherever it decided before whether commands may go out, and sends commands only when the gate is `OPEN`, as before;
  - before each tick it sets the loop configuration's `assume_landed` to whether the gate is not `OPEN`;
  - it passes the gate to the telemetry;
  - the reopening of the gate keeps the router reset with pacing as it is.

## Acceptance criteria

1. `ctl_gate_state`, as rows of one table test covering all eight combinations of the three flags, with the order above deciding.
2. `ctl_gate_name` for each member and for `(ctl_gate)9`.
3. The telemetry:
   - the outputs count is 4;
   - `outputs/blocked` gives `disabled` for a disabled gate and `-` for an open one.
   
   The earlier rows of `test_telemetry.c` that state the outputs count change by the owner's leave to 4, and no other row changes.
4. The loop with `assume_landed` true never reaches `FAULT`: a test runs `ctl_loop_tick` with no confirmation for 20 decision ticks with `assume_landed` true, and the router's status is never `FAULT`. The same run with it false reaches `FAULT`, as before.
5. `make firmware` compiles both boards.
6. `make check` stays green.
7. docs: `docs/control/firmware.md`, in *What reaches the pump*, names the gate and its reasons, the assumed landing while blocked and why, and the entry `outputs/blocked`. Its telemetry table lists the new entry.

## Smoke test

```sh
make firmware | grep -c -E '^(esp8266|esp32):'
```

Expect: `2`

## Journal

1. 2026-11-12 human: created.
2. factory: started doing as heishamon.
3. builder: Built the command gate as pure C, the `outputs/blocked` telemetry entry, and the glue change that turns on assumed landing while commands are blocked. `make check` is green: lint, test (25 test programs, device replay 0 mismatches, shadow), device, firmware for esp8266 and esp32.

   Paths:
   - New: `HeishaMon/src/control/glue/ctl_gate.h` and `ctl_gate.c`. `ctl_gate_state` returns the first gate that holds, in the order DISABLED, LISTEN_ONLY, NO_OPTIONAL_PCB, then OPEN. `ctl_gate_name` returns `-`, `disabled`, `listen_only` and `no_optional_pcb`, and `invalid` for any other value.
   - `HeishaMon/src/control/glue/ctl_telemetry.h` and `.c`: the source gains `ctl_gate gate`, and `outputs/blocked` is the 4th and last output. `put_output_entry` now takes the whole source instead of only the record.
   - `HeishaMon/src/control/glue/ctl_firmware.cpp`: the file's own `gate` enum and `gate_of` are replaced by `ctl_gate_state(settings.enabled, listen_only, optional_pcb)`. "Unknown before the first pass" is now a `gate_known` flag instead of an enum member. Before each tick the glue sets `config.assume_landed = current != CTL_GATE_OPEN`. It sends commands only while the gate is OPEN, as before. It passes the decision's gate to the telemetry. Reopening the gate still resets the router with pacing, unchanged (`ctl_router_reopen`).
   - New: `tests/control/test_gate.c` holds:
     - the 8-row table of all flag combinations (criterion 1);
     - each name and `(ctl_gate)9` (criterion 2);
     - 20 decision ticks with no confirmation (criterion 4): with `assume_landed` true the router never reaches FAULT, with it false it does.
   - `tests/control/test_telemetry.c`, changed by the owner's leave:
     - the outputs count goes from 3u to 4u;
     - the refused output index moves from 3 to 4, because index 3 is now valid;
     - the `outputs[]` row table of `test_entries` gains `{"outputs/blocked", "-"}`, because CHECK_GROUP compares the count with the table's size;
     - `set_fixture` sets `gate = CTL_GATE_OPEN`, which the new field needs;
     - new `test_blocked` checks `-` for an open gate and `disabled` for a disabled one.
     
     No other row changed. The reviewer should confirm that the extra row and the fixture line are within the owner's leave.
   - `docs/control/firmware.md`, *What reaches the pump*: the gate, its reasons in order, `outputs/blocked`, and a new paragraph on assumed landing while blocked and why. The reopen paragraph now says the router counts its commands as landed while blocked. The Outputs table lists the new entry and says 4 entries.
   - `docs/control/decisions.md`: new entry "A blocked board assumes its commands landed, and names why it is blocked".

   Decisions:
   - `status/enabled` stays as it was, meaning "the gate is open". It is now derived from the stored gate, and the stored `decision_enabled` is removed.
   - The order of the reasons follows the story, so listen-only is now checked before the missing optional PCB emulation. Before, it was the other way round. The log line for each reason is unchanged; only which reason a board with both reads differs.
   - The criterion-4 test lives in `test_gate.c` and not in `test_loop.c`, because `test_loop.c` is another story's test.
   - Rejected:
     - faking the confirmation in the glue, which would put a second rule beside `assume_landed`;
     - not stepping the router while blocked, which would hide what the loop would send.

   Smoke test: `make firmware | grep -c -E '^(esp8266|esp32):'` printed `2`, which matches its Expect line.

   Noticed, not touched: `ctl_loop_confirm` is still called with `known = optional_pcb && !listen_only`. While the gate is open this is always true, and while it is blocked `assume_landed` overrides it. So it is now redundant, but I left it as it was so the confirmation path stays as before. `ctl_settings_to_loop` resets `assume_landed` to false on every settings change; that is harmless, because the glue sets it again before each tick.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: no findings

   What I checked, from the code rather than the builder's account:

   1. **Acceptance criteria.**
      - Criterion 1: `tests/control/test_gate.c` `test_state` has all eight flag combinations, and their expected values follow the order DISABLED, LISTEN_ONLY, NO_OPTIONAL_PCB, OPEN.
      - Criterion 2: `test_name` checks every member and `(ctl_gate)9`.
      - Criterion 3: `test_telemetry.c` checks the count of 4, and `test_blocked` checks `-` for an open gate and `disabled` for a disabled one.
      - Criterion 4: `faults_unconfirmed` runs 20 decision ticks with no `ctl_loop_confirm` and checks every record. With `assume_landed` true the router is never `FAULT`; with it false it reaches `FAULT`. The loop bound is fixed at 200 steps, and the test also checks that exactly 20 decisions ran.
      - Criteria 5 and 6: the journal reports `check: green` and the smoke test printing `2`.
      - The rule in `ctl_loop.c` `step_router` (with `assume_landed`, `known` is true and the state is the router's `sent_state`) matches the documented behaviour.
   2. **Architecture.**
      - `ctl_gate.c` is pure C99, includes only its own header and `stdbool.h`, and its public names use the `ctl_`/`CTL_` prefix.
      - The Arduino code stays in `ctl_firmware.cpp`.
      - No upstream file is changed.
   3. **Embedded safety.**
      - Nothing allocates memory or recurses.
      - `outputs/blocked` is written through `ctl_text_put` with the existing fit check.
      - The page (`ctl_web.cpp`) gets its source only through `ctl_firmware_last_decision`, which sets `gate`, so no source reaches the telemetry with that field unset.
   4. **Quality.**
      - The file's own gate enum and `gate_of` are replaced, not duplicated.
      - `gate_known` keeps the log line on the first pass and the reopen only after the first pass, as before.
      - `status/enabled` is now derived from the stored gate, so the two cannot disagree.
   5. **Work item hygiene.**
      - The docs in `docs/control/firmware.md` match the code:
        - the order of the reasons;
        - the names `-`, `disabled`, `listen_only`, `no_optional_pcb`;
        - the log texts, which match `gate_text`;
        - assumed landing while blocked;
        - the 4-entry outputs table with `outputs/blocked`.
      - The decision entry in `docs/control/decisions.md` names what was rejected.
      - The journal states the main decision and the rejected options.
   6. **Earlier tests.**
      - `tests/control/test_telemetry.c` (M) was changed under the story's own leave in criterion 3, which lets the rows that state the outputs count change to 4:
        - in `test_counts`, the outputs count goes from 3u to 4u;
        - in `test_refusals`, the refused index past the end moves from 3 to 4. That row states the count.
        - the `outputs[]` table in `test_entries` gains `{"outputs/blocked", "-"}`, because `CHECK_GROUP` checks the table's size against the count, so the table also states the count;
        - `set_fixture` sets `gate = CTL_GATE_OPEN`. The fixture is a stack variable that is not zeroed, so the new field needs it; it is not a row;
        - `test_blocked` is new, and adding a test is allowed.
      
      No other earlier row changed. I accept these as within the leave.
