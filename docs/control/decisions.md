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
