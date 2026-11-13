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

## 2026-11-11: Defrost masks the produced heat as hot water does

**Decision.** `P_PRODUCED` is reported as 0 W while the pump defrosts, by the same mask as the
hot-water valve; a reading is given when the mask as a whole switches, so a flag that changes
while the other still masks gives none. A switch gives a reading even when the stored heat is
0 W.
**Reason.** A defrost draws heat from the water circuit to the outdoor unit; like hot-water heat,
none of it enters the rooms ([A-3] in `algorithm-plan.md`). Emitting on the mask's switch, not on
each flag's, keeps one reading per change of what the core sees.
**Rejected.** Ignoring defrost because it is short, which counts heat the rooms never got; and
emitting on every flag change, which repeats a 0 W the core already has.

## 2026-11-11: A negative pump or PV value is refused, not clamped

**Decision.** A produced heat below 0 W or a PV power below 0 W is refused like any value outside
its bound, and the core sees no reading; it is not clamped to 0.
**Reason.** A meter that reports a negative power is broken or misread, and a clamped 0 W would
look like a valid measurement and keep the signal fresh. Refused, the core's own staleness
handling takes over.
**Rejected.** Clamping to 0, which hides the fault behind a plausible value.

## 2026-11-11: The pump's flags accept exactly 0 and 1

**Decision.** `ctl_pump_valve` and `ctl_pump_defrost` refuse any value but 0.0 and 1.0, and keep
the flag as it was. `ctl_pump_init` sets `heat_w` to 0.0 beside `has_heat = false`.
**Reason.** HeishaMon publishes both states as 0 or 1; another value is no known state, and
guessing could unmask the heat of a hot-water run. A defined `heat_w` keeps the state comparable
as a whole.
**Rejected.** Treating any non-zero value as set, which would read a corrupt payload as a state.

## 2026-11-11: The solar gain refuses a product that is not finite

**Decision.** `ctl_solar` refuses, and gives no reading, when `pv * gain` is not finite, even
though the PV power and the gain were each accepted.
**Reason.** The configuration bounds `solar_gain_factor` only from below, so a finite gain near
the largest `double` turns an accepted PV power into an infinite `P_SOLAR`; a value from outside
is checked with `isfinite` before the core uses it.
**Rejected.** An upper bound on the gain in the adapter, which would be a second rule beside the
configuration's and a guess at the largest sensible gain.

## 2026-11-11: The heat/cool polarity is a setting with no default

**Decision.** `heat_cool_heat_value` says which value of `SetHeatCoolMode` means heat; it is -1
(unset) by default, and while it is unset the router sends only `SetExternalThermostat1State`.
No demand never sends `SetHeatCoolMode`.
**Reason.** Nobody knows yet which value the emulated frame takes for heat on this pump, and a
wrong guess would cool when the core asks for heat. Unset, the pump keeps the mode it has.
**Rejected.** A default of 0 or 1, which is a guess at the polarity; and sending the mode with no
demand, which has no direction to send.

## 2026-11-11: The router sends on change, paced, and confirms with bounded repeats

**Decision.** The router sends a new thermostat state only when the wanted state changes and at
least `min_change_interval_s` (300 s) after the last new state; it repeats a state the pump has
not confirmed after `retry_interval_s` (120 s), up to `max_retries` (3) times, and then reports a
fault until the state lands or a new state is sent. Pacing counts from the last new state, not
from a repeat. An unknown pump state counts as not confirmed.
**Reason.** Each write wears the pump's EEPROM, and upstream asks for a few changes per hour per
setting at most (`firmware-design.md` §4); commands are asynchronous and can be dropped, so a
change must be checked and a lost one sent again, but not forever.
**Rejected.** Sending on every step, which the emulated frame makes needless and the EEPROM
cannot bear; repeating without a bound, which would hide a broken link behind endless writes;
and pacing from the last repeat, which would let dropped commands delay a change of demand.

## 2026-11-11: The router starts at no demand and is pure

**Decision.** A fresh router wants no demand and has sent nothing, so its first step sends `0` at
once. It takes the demand, the pump's reported state and an instant and returns at most two
commands in a struct; the glue sends them. A step with a `now` that is not finite, or with a
configuration `ctl_router_config_valid` refuses, sends nothing and changes nothing. A state that
lands while the demand already wants another one leaves the status and the retries as they are.
**Reason.** A boot starts at no demand until the core has seen fresh inputs
(`firmware-design.md` §4), and a pure router is tested on the host like the core. An invalid
configuration could write a value the pump does not know; refusing it as a whole matches the
room average. While a change is pending the router is still waiting to send it, so `IDLE` would
be wrong.
**Rejected.** Waiting for the core's first demand before sending anything, which would leave the
pump in whatever state the last run left it; and calling upstream's command sender from the
router, which would tie it to the Arduino framework.

## 2026-11-11: The loop writes numbers without `printf` of floats

**Decision.** The record's numbers are written by `ctl_format_fixed` with integer arithmetic:
the magnitude times 10^decimals, rounded half away from zero by comparing the exact fraction
with one half, and written digit by digit. Magnitudes of 1e12 and more are refused, so the
scaled value always fits a 64-bit integer.
**Reason.** The ESP8266 and ESP32 libraries may lack floating point in `printf` or pull in a large
one for it, and the shadow's lines must read the same on the host and on every board.
**Rejected.** `snprintf` with `%.2f`, which depends on the library; and `dtostrf`, which exists
only on Arduino and so cannot be tested on the host.

## 2026-11-11: The loop masks cooling by default

**Decision.** `ctl_loop_config_defaults` allows `HEATING` and `NONE` only; the core then answers a
cooling decision with `NONE` and the reason `MASKED`.
**Reason.** Cooling through the energy balance has no field precedent, and the design recommends
the first winter with cooling masked off and cooling enabled as its own commissioning step
(`firmware-design.md` §4 and §9, open item 8). The core's mask exists for exactly this and
clamps the cooling direction's windup.
**Rejected.** Allowing every demand by default, which would cool on the first warm afternoon of
a board nobody configured.

## 2026-11-11: `assume_landed` lets a shadow run without a pump

**Decision.** With `assume_landed` the loop tells the router, at each decision tick, that the
pump reports the state it last sent. It is false by default.
**Reason.** A shadow computes commands and sends none, so nothing could confirm them; its router
would repeat and fault on every change, and its records would show faults of a link that does not
exist. The default keeps the real check for the firmware.
**Rejected.** Leaving the router out of the shadow, which would hide the commands and their
pacing, the very thing the shadow is there to show; and a confirmation the shadow sketch fakes
itself, which would put the same rule in two places.

## 2026-11-11: The loop tells an unchanged flag from a refused one by the adapter's own rule

**Decision.** `ctl_pump_valve` and `ctl_pump_defrost` return false both for a refused payload and
for an accepted flag that left the mask as it was. The loop answers `NO_READING` when the payload
parses to exactly 0 or 1 and `REFUSED` otherwise, the same rule the adapter applies.
**Reason.** The interface asks for the two answers apart, and the adapters were to stay as they
are.
**Rejected.** Changing the pump adapter to return a status, which this work was not to do; and
answering `NO_READING` for both, which would hide a broken source.

## 2026-11-11: A record that does not fit is refused, not cut

**Decision.** `ctl_loop_format_record` writes into the caller's buffer and returns false with an
empty text when the line does not fit, or when a number is beyond what `ctl_format_fixed` writes.
**Reason.** A cut line would look whole in a log.
**Rejected.** Cutting the line at the buffer's end.

## 2026-11-12: The record's buffer holds the longest record

**Decision.** `CTL_LOOP_TEXT_MAX` is 365: the longest line `ctl_loop_format_record` can write
(every number just below 1e12 and rounding up to thirteen digits with a sign, the longest enum
names, both commands filling `CTL_ROUTER_NAME_MAX` and `CTL_ROUTER_VALUE_MAX`) and its terminating
zero. A test builds that record and checks the constant equals its length plus one.
**Reason.** The human chose a size that holds every record without conditions; the cost is about
100 bytes more than 256.
**Rejected.** Keeping 256 and documenting the ranges it holds (instant below 1e8 s, energy and
powers below 1e7), which made "enough for every record" true only under conditions.

## 2026-11-12: The shadow's bridge forwards retained messages

**Decision.** `shadow.py` forwards a retained message like any other and marks it `yes` in the
`retained` column of `inputs.csv`.
**Reason.** The thermostats and HeishaMon publish their values retained, and several report only
on a change: the retained value is the current one, often the only one for many minutes. Dropping
it would keep the shadow in `WAITING_FOR_INPUTS` long after the start, which the firmware glue
will not see, because it subscribes the same way. A retained room value carries its own `ts`, so
the rooms still refuse an old one delivered twice.
**Rejected.** Dropping retained messages, which delays the start by up to a report interval of the
slowest source; and forwarding them with a flag the loop does not have.

## 2026-11-12: The bridge takes its link from `replay.py`

**Decision.** `shadow.py` imports `replay` and uses its `ProcessLink`, `SerialLink`, `wait_ready`
and frame helpers; nothing moved to a shared `link.py`. Its serial link is a subclass of the
replay's that keeps a line the timeout cuts until the line's end arrives: the bridge reads with a
0.1 s timeout, pyserial drops the partial line when it runs out, and a record line takes about
11 ms at 230,400 baud, so about one record in ten would be cut. The replay waits seconds for each
reply and keeps its own reader.
**Reason.** `test_replay.py` replaces `replay.time`, `replay.READY_TIMEOUT_S` and
`replay.REPLY_TIMEOUT_S` to test the reset and the wait for `ready`; moved to another module, those
names would no longer reach the code, and the replay's tests would change.
**Rejected.** A shared `link.py` that `replay.py` imports, which the story allowed but which
would have meant changing `test_replay.py`.

## 2026-11-12: A shadow `K` line checks its form, the start checks the values

**Decision.** A `K` line answers `error` only for its form and the ranges the protocol names: a
room count of 1 to 16, a room index below the count, cooling 0 or 1. A weight, a room timeout or a
gain outside its range is taken, and `G` answers `bad` through `ctl_loop_config_valid`. `K rooms`
resets every weight but keeps the room timeout. A second `G` gives `error`.
**Reason.** One rule for the values, in one place: the loop's own check. The protocol asks for
`G` after `K room_timeout_s 0` to answer `bad`, so the timeout's range cannot be the line's.
Restarting the loop on a second `G` would silently drop every input it holds.
**Rejected.** Checking each value on its own line as well, which would copy the configuration's
rules into the protocol.

## 2026-11-12: The bridge writes a row for every message on a configured topic

**Decision.** `inputs.csv` gets a row for every message on a configured topic, also when it was not
sent: such a row has the reply `skipped`, and an input whose reply never came gets `-`. A message
on a topic no input uses is dropped without a row. A payload counts as ASCII when, with every
carriage return and line feed removed, it holds only printable ASCII; a tab or another control
byte would end or break the line on the board. The CSV files get a header row when they are new.
**Reason.** A night's cadence of a source is read from `inputs.csv`; a skipped message is still a
report of that source.
**Rejected.** Leaving skipped messages out of the file, which would hide a source that sends what
the board cannot take.

## 2026-11-12: The settings are one flat set of names with text values

**Decision.** Every setting the owner can set is a name with a text value (`enabled`,
`room.0.topic`, `hysteresis_wh`, …), set, read and listed through `glue/ctl_settings.h`. A room's
settings are names of their own, listed only for rooms below `rooms`; a lower count keeps the
values above it, so that raising it again brings them back.
**Reason.** The settings file, the MQTT setting topics and a web page each map one name to one
value; with one setter and one getter, the rules and the text forms live in one place, and a
setting topic is `<subtree>/<name>` with no further mapping.
**Rejected.** Nested settings (a JSON document, or a room list as one value), which would need a
parser of their own on the board and make one MQTT topic change several values at once.

## 2026-11-12: Commands are disabled by default

**Decision.** `enabled` is 0 by default: the board runs the loop and publishes what it would do,
but sends no command to the pump until the owner sets `enabled` to 1.
**Reason.** A board fresh from the flash, or one whose settings file was lost, has no room topics
and possibly a wrong polarity; it must not switch the pump on its own. Disabled by default makes
switching the pump the owner's deliberate step.
**Rejected.** Enabled by default, which would let a fresh board send commands before the owner
has seen a single decision.

## 2026-11-12: A setting's number is one the getter writes back exactly

**Decision.** The setter refuses (`BAD_VALUE`) a number with a magnitude of 10¹² or more and a
number with more than three decimals, so that its value survives the getter's text. A JSON object
is refused even though the payload parser reads one. `max_retries` takes any whole number an `int`
holds, and the router's range decides; `rooms`, the flags and `heat_cool_heat_value` have their
own ranges. `trim_limit_k` 0 or below is `BAD_VALUE`, its own rule beside `none`.
**Reason.** The settings file is written through the getter; a value it rounds would change
silently at the next boot. Every value the core takes has at most three meaningful decimals.
**Rejected.** Rounding on set, which would apply a value the owner did not send; and more
decimals in the getter, which would make `0.02` read `0.020000`.

## 2026-11-12: A telemetry entry with a number the fixed format refuses is refused whole

**Decision.** `ctl_telemetry_entry` returns false, writing nothing, when a value holds a number of
magnitude 10¹² or more, as for a buffer too small. NaN and the infinities are written as the fixed
format writes them (`nan`, `inf`). A room without a value writes `-` for `value_c`, and a sent
state below 0 writes `-`. The entries are composed in buffers of the module's own and copied out
only when both fit.
**Reason.** The record's text (`ctl_loop_format_record`) treats such a number the same way; a cut
or empty value would read as a measurement. Such a number never comes from a running board.
**Rejected.** Writing `-` for it, which would hide a broken value as a missing one; and writing
straight into the caller's buffers, which would leave half a text behind on a refusal.

## 2026-11-12: One text writer for the record line and the telemetry

**Decision.** The writer that builds texts piece by piece lives in `loop/ctl_text.h` (`ctl_text`,
`ctl_text_put`, `ctl_text_put_bounded`, `ctl_text_put_number`, `ctl_text_put_input`,
`ctl_text_put_commands`), and the router's status names are public as `ctl_router_status_name`.
Both `ctl_loop_format_record` and the telemetry use them. The writer keeps its text terminated
after every piece.
**Reason.** The record line and the telemetry must write the same numbers, the same `-` for a
missing input and the same command list; two copies would drift apart.
**Rejected.** A static copy in each module; and a test program of its own for the writer, since
the record's and the telemetry's table tests already cover every piece through the public
functions.

## 2026-11-12: The router's confirmation is the emulated frame

**Decision.** The thermostat state that confirms a command is read from the frame HeishaMon
emulates: bits 2 and 3 of byte 6 of `optionalPCBQuery`, where `set_external_thermostat_1_state`
writes it and from where HeishaMon sends it to the pump every second. It counts as known only
while the frame goes out (optional PCB emulation on, not listen-only).
**Reason.** The thermostat input is a field of the frame the board itself sends. None of the
values upstream decodes from the pump carries it back: not the main data frame's, and not the
pump's answer to the emulated frame (the `OPT` topics: water pumps, mixing valves, alarm), so
there is no report of the pump's own to read it from. What the
board sends every second is what the pump is told; a command that never reached the frame
(`send_heatpump_command` drops optional PCB commands while the emulation is off) does not confirm,
and the router repeats and faults as it should.
**Rejected.** A value of the pump's own decoded frame, which does not carry the external
thermostat state; and assuming that every command landed, which would hide a dropped command.

## 2026-11-12: Opening the command gate reopens the router, paced

**Decision.** When commands may reach the pump again (`enabled` set to 1, the emulation switched
on or listen-only switched off) after the first pass, the glue reopens the router with
`ctl_router_reopen`: it forgets the state it sent, its repeats and its fault, but keeps the
instant of its last send as the start of the pacing interval. The wanted state goes out once,
and not sooner than `min_change_interval_s` after that send. While the gate is shut the router
steps as if it sent, and the telemetry shows what it would send. The owner chose this.
**Reason.** While the gate is shut nothing confirms, so the router repeats and faults; a faulted
router sends nothing until the demand changes, which can take hours, and the pump would stay
without the demand the owner just enabled. Keeping the pacing means that toggling the gate never
writes the pump's EEPROM more often than the pacing allows. The last send the router knows may be
one that never went out; pacing from it is never sooner than from the last real one.
**Rejected.** Starting the router fresh with `ctl_router_init`, which sends at once and skips the
pacing on every reopening; leaving the router as it is, with the gap above; and stepping the
router only while the gate is open, which would hide in the telemetry what the loop would send.

## 2026-11-12: The settings file holds text values and is replaced whole

**Decision.** `/control.json` holds JSON strings only; a number or any other JSON value is a
refused pair, logged and skipped. The glue writes the whole file to `/control.json.new` and
renames it over the old one. An unknown setting name over MQTT is logged and answered with
nothing, since there is no value to echo. `status/enabled` is 1 only when commands may reach the
pump: `enabled` 1, the emulation on, not listen-only.
**Reason.** The setter takes text, and the getter writes every value back as text, so a file the
board writes always reads back the same; a reset while writing leaves the old file whole.
`config/enabled` already shows the setting; the status says what happens to the commands.
**Rejected.** Converting JSON numbers to text, which would read back a value the getter never
wrote; writing in place; and echoing an empty value for an unknown name, which would create a
retained topic for a setting that does not exist.

## 2026-11-12: The control page reports every answer, and saves the file only on a change

**Decision.** A save from `/control` lists as *accepted* every setting the setter accepted, an
unchanged value included, and as *refused* those it refused; a save that held no setting says so.
A save from the page writes the settings file only when a value text changed; a setting topic
over MQTT still writes it on every `OK`. Both share one way to change settings in the glue
(`ctl_firmware_change_begin`, `ctl_firmware_change`, `ctl_firmware_change_end`), the caller
choosing whether every `OK` counts as a change. A save from the page unsubscribes and
resubscribes every input topic, and publishes every setting under `config/` when anything
changed. A value upstream hands over in pieces is joined again, and refused whole when the
joined value is too long for any setting. The page shows each telemetry entry by its MQTT
subtopic. A value too long to show escaped in one chunk shows its name without a field. The page
keeps upstream's look by including `htmlcode.h` for its header, CSS, body start, menu script
and footer, and fills the side menu with links of its own, as every upstream page does.
**Reason.** The page shows what the last save accepted and refused, and an unchanged value is
accepted. The form posts every setting on every save, so writing the file only on a change spares
the flash a write per identical save; a setting topic carries one value the owner chose to send,
and its behaviour stays as it was. The form holds every topic, and the old topics must be
unsubscribed before they are replaced, so the save cannot wait to know which moved. The form
posts every topic on every save, so keeping only the last piece of a long value would replace a
topic with its tail on any save. The subtopics are the names the owner already knows from MQTT,
and they keep status and outputs apart in one table. A field missing from the form is a field
the save does not touch, which is safer than a cut value.
**Rejected.** Listing only the settings whose value changed, which is not what the page promises;
saving over MQTT only on a change, which changes a behaviour no one asked to change; comparing
the old and new topics to subscribe only what moved, which needs a copy of the settings on the
stack; cutting a joined value that is too long, which would save a topic no one wrote; showing a
value across chunks so that any length fits; and a page of its own CSS, which would look unlike
HeishaMon. `htmlcode.h`'s texts are `static`, so the few the page uses exist twice in flash;
reaching upstream's copies would need a change to upstream's files.

## 2026-11-12: The save of the control page is pure C, with the setter passed in

**Decision.** Keeping the posted fields, joining a value's pieces, applying the fields in the
order of the settings list and writing the report's HTML live in `glue/ctl_form.c`, pure C99 with
a host test. `glue/ctl_web.cpp` keeps only upstream's steps: the routes, the chunks of the page,
the redirect and the buffer's life. `ctl_form_apply` takes the setter as a function and a context,
so the page passes the glue's `ctl_firmware_change` and the test passes `ctl_settings_set` on
settings of its own; the report's opening, rows, closing and empty-save line are its items, so
the page writes them like any other section's rows.
**Reason.** Everything the save decides was Arduino C++ that only `make firmware` compiles, and
two faults got past review there. With the setter passed in, the module needs no file, no topic
and no state of the glue, and the test calls the real settings setter.
**Rejected.** Applying to a copy of the settings and letting the glue compare old and new, which
needs a second settings struct on the stack and loses the setter's per-field log; keeping the
report's HTML in the page, which would leave the empty-save line untested.

## 2026-11-12: The pump's values may come from MQTT, and then only from there

**Decision.** Four topic settings, `outdoor_topic`, `heat_topic`, `valve_topic` and
`defrost_topic`, follow `pv_topic` in the settings list. `ctl_dispatch_topic` checks the rooms
first, then `pv_topic` and the four in that order, from one table, so a topic two settings share
goes to the first. While any one of the four is set, `ctl_firmware_frame` hands nothing, not even
the values whose topic is empty. The glue keeps the last source it logged and logs again only when
it changes, at setup and at the end of every change of settings. Any setting name ending in
`_topic` or `.topic`, and `rooms`, moves the subscriptions on a change over MQTT.
**Reason.** A board shadowing another HeishaMon must not mix in a pump of its own; one switch for
all four is simpler to reason about than four. The topic settings share one field lookup in
`ctl_settings.c`, so the setter and the getter treat every topic the same.
**Rejected.** Switching each value separately (a frame value for every empty topic), which mixes
two pumps on a board that has one; a separate on/off setting, which the story does not name and
which the topics already imply; logging on every frame skipped, which floods the log.


## 2026-11-12: A blocked board assumes its commands landed, and names why it is blocked

**Decision.** The glue computes the command gate with the pure `ctl_gate_state`, which gives the
first reason that holds in the order `enabled` 0, listen-only mode, no optional PCB emulation,
and otherwise open. Before each tick it sets the loop's `assume_landed` to whether the gate is
blocked, and the telemetry names the reason as `outputs/blocked`, `-` while open. The gate is
kept in the telemetry source beside `enabled`, which stays as the open flag of `status/enabled`.
**Reason.** A blocked board sends nothing, so nothing confirms its commands; its router repeated
its first send and ended in `FAULT`, which is no fault of anything. The shadow's
`assume_landed` already solves the same problem, so the glue uses it rather than a second rule.
The reason checked first is the owner's own switch, so that a board with commands disabled reads
`disabled` whatever its other flags. Reopening still resets the router with pacing, so the wanted
state reaches the pump once the gate opens.
**Rejected.** Not stepping the router while blocked, which would hide what the loop would send;
a confirmation the glue fakes from the router's own sent state, the same rule as
`assume_landed` in a second place; and dropping `status/enabled` for the new entry, which would
change an entry other tools may read.


## 2026-11-12: Frames from another HeishaMon's log

**Decision.** `ctl_framelog.c` reads a log line only after its first `): `. A line is a sent
frame's mark when its text contains `sent bytes: ` anywhere, not only at its start, since upstream's
ESP32 code logs `heatpump request query sent bytes: <n>`. A `Received <n> bytes data` line with
a count of 0 or above `CTL_FRAMELOG_MAX` starts nothing but still ends a frame in progress as
dropped, because the board did receive a new frame. A `data:` line with no bytes adds nothing.
The glue counts as dropped every frame the parser drops and every whole frame that is no main
data frame; whether a frame is one is the pure `ctl_framelog_is_main_data`, so that a host test
holds the rule that keeps any other frame from upstream's decoder. It resets the parser whenever
the topics move. `frame_log_topic` does not count as a pump topic in
`ctl_settings_pump_from_mqtt`, so the decoded frames reach the loop.
**Reason.** The parser must not take a frame of the board's own, and the board names the frame
before the mark on one of its two builds. A new `Received` line means the old frame's remaining
lines will never come, whatever its count. Two counters are what the telemetry names, and a frame
read and thrown away is dropped from the loop's view.
**Rejected.** Matching `sent bytes: ` only at the start of the text, which misses the ESP32's
query lines; a third counter for whole frames of another kind, which the story does not name;
leaving a frame in progress alive across an out-of-range `Received` line; reading the
`Checksum and header received ok!` line as the end of the frame, which the board logs only after
its own checksum check and which the parser's own check makes redundant; keeping the main data
frame test in the `.cpp`, where only `make firmware` compiles it and no test reaches it.


## 2026-11-12: The payload first

**Decision.** `handle_setting` copies the payload into its own buffer, or refuses it, before it
begins the change; every handler of `ctl_firmware_mqtt` reads the topic and the payload before
anything that can send. A host test, `tests/control/test_payload_first.c`, reads the glue's
source and fails when a call that can send comes before a handler's last read of the payload.
**Reason.** PubSubClient keeps the received topic and payload in the buffer it also sends from.
`handle_setting` began the change first, and for a setting that moves topics the begin
unsubscribes the old input topics, which overwrote the payload before it was copied: on the
owner's dev board, `jeisha/log` sent to `frame_log_topic` was stored as `/1/ACTUAL_`, ten bytes of
a room topic. The input path hands the payload to the loop and the frame log path to the parser,
both before anything sends, and the dispatch copies the setting's name out of the topic, so only
`handle_setting` needed the change. The `.cpp` compiles only in the firmware build, so the test
holds the order in its text.
**Rejected.** Copying the whole message at the top of `ctl_firmware_mqtt`, which costs a second
buffer of the client's size for every message, upstream's included; moving the copy into a pure
`.c` helper, which a test could call but which would not hold the order of the calls in the
`.cpp`; leaving the change untested, since only the firmware build compiles the glue.

## 2026-11-13: Smart Grid as the house's switch

**Decision.** The router gets a `smart_grid` mode in which it sends `SetSmartGridMode` alone, `0`
for heating or cooling demand and `1` for no demand, and confirms it from bits 4 and 5 of byte 6
of the optional PCB frame. In that mode the router's state is the Smart Grid mode itself, so a
change between heating and cooling is no new state and sends nothing. A dev board can send its
commands over MQTT to another HeishaMon (`command_topic`) and confirm them from the optional PCB
frames that board logs as sent; the state counts as known while such a frame is at most 60 s old.
Value `1` is upstream's "Capacity 1"; it stops room heating on the owner's pump only because the
owner set Capacity 1 on the pump to a heating curve that never heats, and hot water goes on.
Another pump needs the same installer setting before `smart_grid` is used, or value `1` only
lowers its capacity.
A change of `smart_grid` or `command_topic` reopens the router, and a change of any topic setting
forgets the other board's last frame. `frames_dropped` counts only frames that broke, sent or
received: a whole frame that is no main data frame no longer counts as dropped, which replaces
that part of the frame log entry above, and a whole sent frame counts as neither.
**Reason.** The owner's pump ignores the external thermostat input of the optional PCB and obeys
Smart Grid mode, which the owner's Node-RED uses today. Smart Grid mode carries no heating or
cooling, so resending it on that change would only wear the pump's EEPROM. What the router sent
before a change of route or kind says nothing of what the pump now holds, and a frame of an old log
topic may come from another board.
**Rejected.** Keeping the thermostat state inside the router and mapping it to Smart Grid mode
only on the way out, which sends `0` again when heating turns to cooling; sending both the
thermostat state and Smart Grid mode, which the story forbids; a retained command message, which
the other board would run again at every reconnect; still counting a whole received frame that is
no main data frame as dropped, which the other board's optional PCB answers would raise every
second, so that the dropped count would no longer show a log that loses lines; sending `2` for
no demand, upstream's "heat pump and hot water off", which stops hot water too.

## 2026-11-13: The pump topics are removed; the frame log feeds a board without a pump

**Decision.** The settings `outdoor_topic`, `heat_topic`, `valve_topic` and `defrost_topic` are
gone, with `ctl_settings_pump_from_mqtt`, the dispatch's pump-topic inputs and the glue's log of
where the pump's values come from. `ctl_firmware_frame` always hands the pump's values of a main
data frame to the loop, whether the frame came from the board's own pump or from another
HeishaMon's log. A `/control.json` that still names one of them loads as before: the name is
unknown to the setter, so its pair is logged as refused and skipped, and the next write leaves it
out.
**Reason.** The topics were a bridge for a board without a pump. Since the frame log, such a board
decodes the other board's frames itself, and a board on a pump reads its own; nothing needs them,
and a value left in a production board's file would cut its loop off from the pump.
**Rejected.** Keeping the topics and ignoring them, which leaves settings that do nothing on the
page and in the telemetry; refusing a file that names them, which would reset a production board
to its defaults; a new JSON reader and writer for the file in place of ArduinoJson, so that a
host test could load the text itself, which a cleanup does not put onto the production board; the
host test feeds the setter the pairs the loader hands over instead.

## 2026-11-13: The save result in the settings table's rows

**Decision.** The settings table of `/control` has four columns: the name, the field, a
description and the save result, and the separate table of the last save is gone. The report keeps
one byte of result per applied name (`ctl_save_result`): saved, unchanged, or refused with the
setter's reason; a field too long for any setting counts as `not a valid value`. A refused row is
written in the page's orange (`var(--orange)`) by an inline style, since upstream's CSS has no
class for a table row and its file stays as it is. The fourth column's heading is *Save result*,
since no text of the page may name the removed table. The results show on the page that follows
the save only: the glue empties them once that page is written, so a reload shows none. A room's
description names the room by the number in its setting's name, counted from 0.
The descriptions are `ctl_settings_describe`, one table indexed by the setting, with `#` where a
room's number goes. On the ESP8266 the table is `PROGMEM` and read with `pgm_read_byte`, from the
toolchain's own C library header `<sys/pgmspace.h>`, included only when `ESP8266` is defined;
elsewhere, the host included, it is a plain constant. Each text has a fixed slot of
`CTL_SETTINGS_DESCRIPTION_MAX` bytes. A text longer than its slot stops the build, but C accepts
a text of exactly the slot's length without its closing zero; `ctl_settings_describe` then refuses
it for a buffer of the slot's size, and `test_descriptions.c` fails.
**Reason.** The result belongs beside the field it is about, and a setting sent unchanged has
nothing to report. The description table is 21 × 192 = 4032 bytes, all of it RAM on the ESP8266
as plain constants. The setting's own module resolves the names already, so the table sits beside
it and a host test reaches it through the same names.
**Rejected.** The descriptions in `ctl_web.cpp` as `FLASHPROG` texts, which no host test can
reach; a second table of names for the descriptions, which could part ways with the setter's;
reading flash with hand-written aligned word loads, which is what `pgm_read_byte` already does;
keeping the results until the next save, which shows a stale `saved` on every later visit; a CSS
class for refused rows, which needs a change to upstream's `htmlcode.h`; numbering rooms from 1,
which would differ from the names `room.0.topic` and so on beside them.
