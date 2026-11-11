# The control core in C, proven against the Python reference

## Goal

The fork gets the control core as plain C99 under `HeishaMon/src/control/core/`: the same law as the Python reference, with the same decisions on the same inputs. It is for the owner, who wants the loop to run on the board that holds the wire to the heat pump. When the feature is done, a host program replays the thirteen golden traces of the Python core through the C core, and every recorded value agrees bit for bit on every line. Nothing is wired into the firmware yet. Done when every story below is merged and `make check` is green.

## Scope

- The enums of the core and the configuration with its defaults and its validation.
- The value types: reading, inputs, balance, ledger, decision.
- The signal table: held values, ages, freshness, fallbacks, and the resolution into inputs.
- The energy balance, the relay with its hysteresis, and the anti-windup clamp.
- The controller: the initial state, `advance`, `observe`, `set_allowed` and `evaluate`.
- A trace runner under `tests/control/` that replays every trace under `tests/control/data/` and compares every value after every call.
- `docs/control/core.md`, the C interface of the core, and `docs/control/decisions.md`.

## Out of scope

- `restore` and the snapshot type: the board stores no state (decision F-5 of `docs/control/firmware-design.md`).
- The adapters, the router, the clock, the configuration file and the telemetry: later features.
- Any hook in an upstream file, and any build for a board.
- The mutable `Controller` facade of the Python core: the C core is functions over one state struct.
- A change to a trace or to the Python reference: both come from the Python project and are replaced as a whole.

## Stories

The build order; start one at a time, the next when the one before is archived in `done/`.

- S0001: The core's enums and its configuration with validation
- S0002: The value types, the signal table and the resolution into inputs
- S0003: The energy balance, the relay and the clamp
- S0004: The controller: advance, observe, set_allowed and evaluate
- S0005: The trace runner over the thirteen golden traces

Each of S0001 to S0004 carries its own table-driven tests, taken from the rules of `docs/control/reference.md`. S0005 comes last because a trace needs the whole core. A difference it finds in the core is fixed in S0005 itself, and its journal names the module and the rule that was wrong.

S0002 to S0005 are written when the story before is merged, so that each builds on the names and the decisions that exist by then.

## The human's changes

None are open; the write paths are checked against each story when it is written. Already in the fork, from the setup:

- The reference the builder ports from: `docs/control/reference.md`, `docs/control/algorithm-plan.md`, `docs/control/firmware-design.md`, `docs/control/golden-traces.md` and the Python core under `docs/control/python-reference/`.
- The golden traces, `tests/control/data/*.trace.gz`. `make test` unpacks them and names their folder in `CTL_TRACES`.
- `-ffp-contract=off` in the `Makefile`'s compiler flags.

## Decisions taken for the human

1. The fork's own folders are named `control` (`HeishaMon/src/control/`, `tests/control/`, `docs/control/`), and public names start with `ctl_`. Taken in the setup pull request.
2. The core may include `stdbool.h`, `stdint.h` and `stddef.h` beside `math.h`. The firmware design allows only `math.h`; these three declare types and do no input or output. Taken in the `Makefile`'s `lint`.
3. The core computes in `double`, with the operand order of the Python source and without fused multiply-add, and the traces are compared exactly, with no tolerance. This follows `docs/control/golden-traces.md`, section 1. The ESP32-S3 computes doubles in software; at one call every ten seconds that costs nothing that matters.
4. Every `.c` file under the control folder is host-compilable; code that needs the Arduino framework is a `.cpp` file. Taken in `CLAUDE.md`.
5. The pipeline is the thin line: a builder and a reviewer. The traces and the reference's rules are the tests, so a separate tester has little to write.
6. The C configuration has no `state_max_age_s` and no `persist_interval_s`; a trace's header carries them, and the trace runner ignores them, as the format allows. Taken in S0001.
7. The enums give their names as the Python member names (`HEATING`, `NO_DEMAND`), which the traces write, not as the Python values (`heating`). Taken in S0001.
