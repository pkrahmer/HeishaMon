# Decisions

One entry per decision of the fork's control code: the date, the decision, the reason and what
was rejected.

## 2026-11-11: The core computes in `double`, in the Python reference's operand order

**Decision.** Every number in the core is a `double`, and every expression keeps the operand order
of the Python source.
**Reason.** Python's `float` is an IEEE-754 double, and the golden traces are compared bit for
bit; a different order, a `float` or a fused multiply-add would round differently.
**Rejected.** `float` for the board's smaller footprint: it cannot match the traces.

## 2026-11-11: `trim_limit_k = None` becomes `trim_limit_enabled = false`

**Decision.** The configuration has a `bool trim_limit_enabled` beside `trim_limit_k`; a trace's
`none` maps to false. While it is false, `trim_limit_k` is not checked at all.
**Reason.** C has no optional `double`, and an unchecked value while disabled lets a board keep the
last limit in storage while the limit is switched off.
**Rejected.** NaN or a negative value as "no limit": a sentinel inside the number hides a bad
value as a deliberate setting.

## 2026-11-11: The per-signal mappings become arrays indexed by `ctl_signal`

**Decision.** `max_age_s` and `fallback_w` are `double[CTL_SIGNAL_COUNT]`. Every signal has an
entry, so no key can be missing; `fallback_w` of a required signal exists and is unused.
**Reason.** No dynamic memory, and a lookup that cannot fail needs no fallback chain.
**Rejected.** Partial maps with defaults behind them, as in Python: they need a search and a
"missing" case that the board would never use.

## 2026-11-11: `ctl_config_validate` returns bits, not texts

**Decision.** Validation returns a `uint32_t` with one `ctl_config_problem` bit per bad field.
**Reason.** No dynamic memory and no formatting in the core; the caller turns bits into messages.
**Rejected.** A list of texts, as in Python.

## 2026-11-11: No `state_max_age_s` and no `persist_interval_s`

**Decision.** The configuration has neither field.
**Reason.** The board stores no controller state across a restart, so nothing reads them.
**Rejected.** Keeping them unused for symmetry with the Python reference.

## 2026-11-11: No function checks for a null pointer

**Decision.** The caller guarantees every pointer argument; no function of the core checks it.
**Reason.** Every caller holds its configuration in a variable it owns; a check would add a status
to functions that otherwise cannot fail.
**Rejected.** A status return on every function for a case that is a programming error.

## 2026-11-11: A bound on a timing field is compared as in Python, even when the bound is bad

**Decision.** `tick_interval_s` is refused when it is greater than `max_step_s / 2`, and
`integration_interval_s` when it is greater than `tick_interval_s`, whether or not the bounding
field is itself valid. A NaN bound therefore refuses nothing but itself; a bound of 0 or below
also refuses the field it bounds.
**Reason.** It is the Python reference's comparison, and it gives the documented result for a
configuration with several bad fields (a negative `max_step_s` refuses the default tick too).
**Rejected.** Skipping the bound check while the bound is bad: it would hide a tick that no valid
`max_step_s` could fix. Consequently the tests check a zero `max_step_s` or `tick_interval_s`
together with the bit of the field it bounds, and a small positive one with the bounded fields
brought under it, since no default fits under a tiny bound.

## 2026-11-11: The check helper prints doubles with 17 significant digits

**Decision.** `check_double` prints both values with `%.17g`.
**Reason.** Seventeen digits tell apart any two doubles, so a one-bit difference is visible in
the failure line.
**Rejected.** `%g`, which prints 1.0 and the next double above it as the same text.

## 2026-11-11: The check helper's printed lines are read in the test log, not tested

**Decision.** The test of the check helper checks its counts only. The lines a failing check
prints are read in the `make test` output, where a preceding line labels them as expected
failures.
**Reason.** The helper's interface writes to standard output, and C99 cannot portably capture
standard output and restore it; the owner chose to keep the interface as it is.
**Rejected.** A `check_set_output(FILE *)` function that would let a test count the printed
lines in a `tmpfile()`: it grows the interface for the sake of one test.

## 2026-11-11: The signal table is a struct the caller owns and changes in place

**Decision.** `ctl_signal_table` is a plain struct; `ctl_signals_set` changes it in place.
**Reason.** The core has no dynamic memory, and a copy of five readings per set buys nothing a
caller cannot do with an assignment of the struct.
**Rejected.** The Python reference's copy-on-write table, which returns a new table per reading.

## 2026-11-11: A set of signals or of demands is a bit mask in a `uint8_t`

**Decision.** `CTL_SIGNAL_BIT` and `CTL_DEMAND_BIT` give one bit per member; a set is their union
in a `uint8_t`.
**Reason.** Five signals and three demands fit in one byte, and a mask compares and combines
without loops or storage.
**Rejected.** An array of `bool` per member, which needs a loop to compare and is larger.

## 2026-11-11: `ctl_signals_resolve` returns false where Python returns `None`

**Decision.** It returns a `bool` and writes `ctl_inputs` only when it returns true.
**Reason.** C has no optional struct; errors are values, and leaving the output untouched on a
refusal lets a caller keep its previous inputs without a copy.
**Rejected.** A `valid` field inside `ctl_inputs`, which a caller could forget to read.

## 2026-11-11: A held reading has no type of its own

**Decision.** The table's three arrays, `observed`, `value` and `t`, indexed by signal, hold
what Python's `Held` holds.
**Reason.** The arrays match the per-signal arrays of the configuration, and the `observed` flag
replaces the absent key of the Python mapping.
**Rejected.** A `ctl_held` struct with an array of it: one more type for no gain.

## 2026-11-11: The ledger is changed in place and has no `as_dict`

**Decision.** `ctl_ledger_accumulate` adds to the caller's ledger; there is no conversion to a
mapping.
**Reason.** The core has no dynamic memory and no text output; a caller reads the five fields
directly.
**Rejected.** A function that returns a new ledger per step, or that formats it.

## 2026-11-11: No function of the signal table, the balance or the relay checks its arguments

**Decision.** No function takes a null pointer or a signal outside the enum; the caller
guarantees both, and no function checks.
**Reason.** As with the configuration, a check would add a status to functions that otherwise
cannot fail, for what is a programming error.
**Rejected.** A status return, or a silent ignore of a signal out of range.

## 2026-11-11: `ctl_signals_init` sets every value and instant to 0

**Decision.** Beside clearing every `observed` flag, `ctl_signals_init` sets the `value` and `t`
of each signal to 0.0.
**Reason.** Nothing reads them while a signal is not observed, but a fully defined struct can be
compared and copied without reading indeterminate memory.
**Rejected.** Leaving them unset, which is enough for the functions but not for a caller that
compares two tables.

## 2026-11-11: The controller's state is a struct the caller owns and changes in place

**Decision.** `ctl_state` is a plain struct; `ctl_advance`, `ctl_observe`, `ctl_set_allowed` and
`ctl_evaluate` change it in place and return their other results through arguments.
**Reason.** The core has no dynamic memory, and a caller that wants the old state keeps it with
an assignment of the struct.
**Rejected.** The Python reference's frozen `ControllerState`, which each function replaces with
a new one.

## 2026-11-11: An optional instant is a flag beside the value

**Decision.** `t_last` and `t_healthy` each have a `has_` flag; where Python's decision holds
`None` for its inputs and balance, `ctl_decision` has `has_inputs` false and both structs zero.
**Reason.** C has no optional double, and a NaN as the marker would be indistinguishable from a
NaN that a bug let through.
**Rejected.** NaN as "unset", and pointers to the inputs and balance.

## 2026-11-11: `ctl_observe` takes the three parts of a reading as arguments

**Decision.** The signal, the value and the instant are three arguments; there is no reading
type.
**Reason.** A struct used by one function only adds a type, and the caller has the three values
at hand.
**Rejected.** A `ctl_reading` struct mirroring Python's `Reading`.

## 2026-11-11: No function of the controller checks its pointers or its signal

**Decision.** No function of the controller takes a null pointer or a signal outside the enum;
the caller guarantees both, and no function checks.
**Reason.** As with the signal table, a check would add a status to functions that otherwise
cannot fail, for what is a programming error.
**Rejected.** A status return, or a silent ignore of a signal out of range.

## 2026-11-11: The mutable controller, restore and snapshots are not ported

**Decision.** The core has no `Controller` class, no `restore`, no `to_snapshot` and no
`observe_all`.
**Reason.** The state struct is already the mutable cell the Python `Controller` wraps; a loop
over `ctl_observe` is `observe_all`; persistence needs a wall clock and storage, which belong to
the host.
**Rejected.** Porting them now, before the host that would use them exists.

## 2026-11-11: `ctl_state_init` sets the values behind unset flags to 0

**Decision.** `t_last` and `t_healthy` are 0.0 while their flags are false.
**Reason.** Nothing reads them, but a fully defined struct can be compared and copied without
reading indeterminate memory.
**Rejected.** Leaving them unset.

## 2026-11-11: A refused decision zeroes the inputs and the balance

**Decision.** When `has_inputs` is false, `ctl_evaluate` sets every field of the decision's
`inputs` and `balance` to 0.
**Reason.** A decision is then fully defined whatever the outcome, and a caller that logs it
without looking at `has_inputs` logs zeros, not what an earlier decision left there.
**Rejected.** Leaving them as the caller passed them.

## 2026-11-11: No criterion of the controller disagreed with the Python reference

**Decision.** Every acceptance criterion of the controller was checked against
`docs/control/python-reference/controller.py`; none disagreed, so none was set aside.
**Reason.** The Python source is the measure where the two differ; recording that they did not
saves the next reader the comparison.
**Rejected.** Nothing.

## 2026-11-11: The golden traces showed no fault in the core

**Decision.** The core was left as it was: all thirteen golden traces replayed with 0 mismatches
on their first run, 332,239 calls in all.
**Reason.** The runner was shown to find a difference before it was trusted with this result:
a copy of `winter_day` with one demand changed and a copy of `edge_cases` with one energy changed
were both reported at the changed line, and the faulty traces under `tests/control/data/bad/`
check the same on every run.
**Rejected.** Nothing.

## 2026-11-11: The trace reader is strict where the format leaves room

**Decision.** The reader refuses: any line other than a blank or a comment after the `end` line;
`fallback_w.` of a required signal; a field separated by more than one space; a `clamped` other
than `0` or `1`. It reads `state_max_age_s` and `persist_interval_s` as numbers and ignores them.
It accepts a set whose names are not in alphabetical order or repeat one, and compares sets by
their bit mask. The line buffer of 1,024 characters counts the line without its newline, so a
line of 1,024 characters is read and one of 1,025 is refused; `trim_limit_enabled` is no key, as
`trim_limit_k none` sets it.
**Reason.** The format asks a reader to reject what it does not recognise; the order of a set
says nothing the core holds, so refusing it would refuse a trace for its spelling.
**Rejected.** Accepting trailing content after `end`, which would hide a concatenated file.

## 2026-11-11: The trace replayer can print to a stream other than standard output

**Decision.** `tests/control/trace.h` declares `trace_replay_to`, which takes the stream the
mismatch lines go to; `trace_replay` calls it with `stdout`.
**Reason.** The runner's test must read the first line a replay prints, and redirecting standard
output needs POSIX calls that strict C99 does not have.
**Rejected.** `dup2` on standard output under `_POSIX_C_SOURCE`, and a global output setting.

## 2026-11-11: A trace that cannot be opened is no format error

**Decision.** `trace_replay` returns false with `format_line` 0 and prints `<path>: cannot open`
for a file it cannot open.
**Reason.** No line of the file was read, so there is no line to name; the printed line and the
false result still fail the test.
**Rejected.** `format_line` 1, which would blame a line that does not exist.
