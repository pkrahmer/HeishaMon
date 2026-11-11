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
`trim_limit_k none` sets it. Each of these refusals has a faulty trace of its own under
`tests/control/data/bad/`, as does every key the replayer compares, so that a reader that quietly
accepted a fault or skipped a key would fail the runner's test.
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

## 2026-11-11: The wire protocol ignores a carriage return before it measures the line

**Decision.** `ctl_wire_handle` drops one trailing `\r` first and then refuses a line longer than
`CTL_WIRE_LINE_MAX`, so a line of 128 characters with a `\r` is handled.
**Reason.** A terminal that ends lines with `\r\n` must reach the same limit as one that ends them
with `\n`.
**Rejected.** Counting the `\r` in the limit, which would make the limit depend on the sender.

## 2026-11-11: A bad `B` ends the begun session and keeps `calls`

**Decision.** A `B` that finds the configuration bad clears the session's begun flag and leaves
`calls` as it was.
**Reason.** The configuration the earlier `B` validated is gone once a `C` changed it, so no call
may run on the bad one; `calls` counts the answers since the last successful `B`, and a refused
`B` starts nothing to count from.
**Rejected.** Keeping the session begun on the old state, which would run calls on a configuration
`ctl_config_validate` refused; and zeroing `calls`, which would make the stats line forget calls
that were answered.

## 2026-11-11: The sketch times only the calls `calls` counts

**Decision.** The sketch resets its time sums at a successful `B` and adds the time of an `O`,
`A`, `M` or `E` only when the reply is not `error`.
**Reason.** `us_total / calls` is read as the mean time of one call; both sums must cover the same
calls for that to hold.
**Rejected.** Timing every line, refused ones and `C` included, which would mix the cost of the
parser's refusals into the core's time.

## 2026-11-11: The replay script reads a whole trace before it sends any of it

**Decision.** `replay.py` reads and checks a trace completely first; a malformed one prints
`<file>:<line>: format error` and nothing of it is sent. A reply that is no answer prints
`<file>:<line>: <request> -> <reply>`. Mismatch values are spelled as the trace spells them:
numbers by Python's `repr`, enums and signal sets by name, a missing balance as `none`. The
script waits for `ready` from the host twin too, and exits with 2 when the link cannot be opened
(no pyserial, no such port), as when no `ready` came.
**Reason.** A trace read in full can be sent sixteen requests ahead, which a serial line at
921,600 baud needs to stay busy; and the file is refused for the same lines the C trace runner
refuses, without half a replay before it.
**Rejected.** Reading line by line like the C runner, which would interleave format errors with
replies in flight.

## 2026-11-11: The script's restart line names what came instead of the reply

**Decision.** `replay.py` prints a break as `<file>:<line>: broken line, restarting (<k> of 2):
<what came>`, where `<what came>` is the broken line in safe form, `<request> -> <reply>` for an
answer that is no answer, or `no reply within 10 s`. A break while the configuration is sent
restarts the trace too and prints the place as `<file>` alone. A third break prints the same line
without `restarting`, then `<file>: failed after 2 restarts`. A reply whose fields have the right
number but do not parse is a break like one with the wrong number.
**Reason.** The person at the board needs to see whether the line dropped characters, the board
answered `error`, or it went quiet; and the configuration crosses the same line as the calls. A
field that does not parse is a damaged line, not a value of the core.
**Rejected.** The bare `broken line, restarting (<k> of 2)`, which hides what broke; and failing a
trace without a restart when `R`, a `C` or `B` breaks.

## 2026-11-11: Printing escapes every byte outside printable ASCII

**Decision.** `replay.py` writes every byte from the link outside `0x20` to `0x7e` as `\xNN`, the
control characters included, and a reply whose frame checks but holds such a byte is broken.
**Reason.** A control character can move the cursor or clear a console as surely as a byte above
`0x7f` can crash its encoder; and no reply of the protocol holds one.
**Rejected.** Escaping only bytes above `0x7f`, which leaves the console to the link's garbage.

## 2026-11-11: The host twin takes `--garble` with at most nine decimal digits

**Decision.** `host_server` accepts no argument, or `--garble` and one to nine decimal digits;
`0` garbles nothing. Anything else, a sign or a longer number included, prints the usage line and
exits with 2. Every reply after `ready` counts, `error` and the stats line included.
**Reason.** Nine digits cannot overflow an `unsigned long`, and no replay comes near a billion
replies; a sign or a letter is a mistake on the command line, which an exit says better than a
run that silently garbles nothing.
**Rejected.** `strtoul`, which takes signs, spaces and overflows quietly.

## 2026-11-11: The script's tests are a Python program beside it

**Decision.** The tests of the host twin's frames and of `replay.py`'s restarts, reset and wait
for `ready` are `tests/control/device/test_replay.py`, which `make test` runs with the sources in
`CTL_SOURCES` and a scratch folder in `CTL_BUILD`. It builds the twin with gcc and runs it and the
script as processes, and it calls the script's own functions with stand-in ports and links. The
twin that breaks replies is `tests/control/device/garbling_twin.py`: it runs the host twin and
breaks one reply, or every reply, in each way a restart answers.
**Reason.** A pipe to a process is not reachable from strict C99, and the script's own functions
are best checked from Python; a proxy around the real twin breaks one reply and leaves every other
one right, so that a trace can pass after its restart.
**Rejected.** A C test that runs the Python test with `system`, which would run it a second time;
and more `--garble` modes in the host twin, which would put a test's needs into the code that
serves the board's twin.

## 2026-11-11: The device replay runs at 230,400 baud with 4 requests in flight

**Decision.** The sketch and the script run the serial line at 230,400 baud, and the script keeps
4 requests in flight.
**Reason.** At 921,600 baud the owner's D1 mini dropped and repeated characters after some 26,000
calls; at 230,400 baud with 4 requests in flight it replayed all thirteen traces. Four requests
keep the board busy between replies without filling its receive buffer.
**Rejected.** 921,600 baud with sixteen in flight, which the frame would catch but which would
restart traces often enough to fail them.

## 2026-11-11: Only a bad configuration fails a trace without a restart

**Decision.** While `replay.py` sends `R`, the `C` lines and `B`, every reply other than `ok` is a
break and restarts the trace, except `bad …` to `B`, which fails the trace at once.
**Reason.** A stale line left from an earlier attempt, or any other reply that is no answer, is the
link's fault; the same configuration, sent again, would be bad again.
**Rejected.** Failing the trace on any reply other than `ok` or `error`, which turned a stale
state line into a failure.

## 2026-11-11: The payload parser reads the sources' shapes only

**Decision.** `ctl_payload_parse` reads a bare number or a flat JSON object with a `"v"` or
`"value"` member and an optional `"ts"`, and refuses everything else: nesting, escapes, a key
twice, a value member given twice under its two names, a timestamp beyond 18 digits. Keys seen are
held in a fixed array sized for the longest payload; nothing is allocated.
**Reason.** These are the only shapes the CCU3 and HeishaMon send; a parser that accepts no more
than that cannot be led to take a value from a payload no source sends, fits the board without
dynamic memory, and is short enough to test row by row.
**Rejected.** A general JSON library (ArduinoJson or a C parser): a new library, dynamic or
pool memory, and a far larger surface that accepts shapes the adapters would then have to refuse
again.

## 2026-11-11: Numbers are converted with `strtod` after their form is checked

**Decision.** The parser first checks a number's form by hand (optional `-`, digits, optional
fraction, optional exponent), then converts exactly those bytes, copied into a zero-terminated
buffer, with `strtod`, and refuses a result that is not finite.
**Reason.** `strtod` rounds correctly to the nearest double, which a hand-written conversion would
have to prove; but on its own it also takes a leading `+`, a leading point, `nan`, `inf` and
hexadecimal. The check in front leaves it only the text it converts the same everywhere; the
firmware never calls `setlocale`, so the point is the decimal separator.
**Rejected.** `strtod` alone, which accepts those spellings; and a hand-written conversion, which
rounds differently from `strtod` for long fractions.

## 2026-11-11: A timestamp of 18 digits at most, leading zeros allowed

**Decision.** `"ts"` is 1 to 18 decimal digits, read into an `int64_t`; leading zeros are taken
as they are, as in the bare number's digits.
**Reason.** 18 digits cannot overflow an `int64_t`, so no overflow check is needed, and milliseconds
since 1970 need 13. The story names digits only, and a leading zero changes no value.
**Rejected.** JSON's rule against leading zeros, which no source breaks and which would add a case
without a reason.

## 2026-11-11: A room payload whose timestamp does not advance is a replay

**Decision.** `ctl_rooms_offer` refuses a payload with a timestamp when the room's last accepted
payload had a timestamp at least as large, and leaves the room's arrival instant as it was. A
payload without a timestamp is always taken and clears the room's stored timestamp (held as 0).
**Reason.** The broker showed room values about 90 minutes old: a thermostat that reconnects, or a
retained message delivered again, repeats an old reading. Taking it would reset the room's
freshness and keep a silent room in the average. The timestamp is the only sign of age a payload
carries, so a payload without one cannot be judged and is taken as new.
**Rejected.** Judging age by the timestamp against the board's clock, which needs wall time the
adapters do not have; and refusing every payload without a timestamp after a timestamped one,
which would lock out a source that changes its shape.

## 2026-11-11: The room timeout is a setting, 1800 s by default

**Decision.** `room_timeout_s` is part of `ctl_rooms_config`, 1800 s by default, any finite value
above 0 accepted; a room is still fresh at an age equal to the timeout.
**Reason.** Room temperatures move slowly, and the thermostats report at irregular gaps; a
generous timeout keeps the average's membership stable. With values seen about 90 minutes old on
the broker, the right limit depends on the installation, so the owner sets it.
**Rejected.** A fixed constant, which a house with slower thermostats could not change without a
new build.

## 2026-11-11: The room average refuses an invalid configuration as a whole

**Decision.** `ctl_rooms_average_at` checks the configuration with `ctl_rooms_config_valid` and
gives no average, with all counts and weights 0, when it is not valid; `ctl_rooms_offer` and
`ctl_rooms_is_fresh` treat an index at or above `CTL_ROOMS_MAX` as out of range whatever the
count says. A replaced room's stored timestamp is 0 when its payload had none.
**Reason.** The story leaves these cases open; a count above the arrays' size must not read past
them, and an average from a configuration that would be refused means nothing.
**Rejected.** Assuming a valid configuration and documenting it as the caller's duty, which a
wrong count would turn into a read past the arrays.
