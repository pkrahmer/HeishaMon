# The loop

The loop ties the core, the adapters and the router together. It lives in
`HeishaMon/src/control/loop/` as plain C99 and, like them, takes values and returns values: it has
no clock of its own, sends nothing and logs nothing. Two callers use it the same way: the firmware
glue, which hands it HeishaMon's values and the MQTT payloads as they arrive and sends the
commands it returns, and the shadow sketch, which runs the loop beside the legacy controller and
only writes its records. Instants are seconds of the caller's monotonic clock (see *The clock*).

The configuration (`ctl_loop_config`) holds the core's, the rooms' and the router's own
configurations, the allowed demands and `assume_landed`. `ctl_loop_config_valid` accepts it when
each of the three parts is valid and `allowed` holds no bit beside the three demand bits.
`ctl_loop_init` starts the loop cold at an instant: fresh state, rooms, pump and router, the
allowed demands set on the core at that instant, no confirmation of the pump's state, and both
ticks due one interval later.

## The inputs (`ctl_loop_input`)

Each input goes through its adapter as it arrives (`adapters.md`); a reading the adapter returns
reaches the core at once with `ctl_observe`, at the instant it arrived.

| Input | Adapter | Reading |
|---|---|---|
| `CTL_INPUT_ROOM` | `ctl_rooms_offer` | none now: the room waits for the decision tick |
| `CTL_INPUT_OUTDOOR` | `ctl_pump_outdoor` | `T_OUTDOOR` |
| `CTL_INPUT_HEAT` | `ctl_pump_heat` | `P_PRODUCED`, masked while on hot water or defrosting |
| `CTL_INPUT_VALVE` | `ctl_pump_valve` | `P_PRODUCED`, only when the mask changes |
| `CTL_INPUT_DEFROST` | `ctl_pump_defrost` | `P_PRODUCED`, only when the mask changes |
| `CTL_INPUT_PV` | `ctl_solar`, with the core's `solar_gain_factor` | `P_SOLAR` |

The answer is one of four:

- `TAKEN`: a reading reached the core, or the room stored its value.
- `NO_READING`: the adapter accepted a valve or defrost flag that left the mask as it was, so the
  core already holds the right heat.
- `REFUSED`: the adapter refused the payload, or the rooms refused the room's value.
- `BAD`: the call itself is wrong, and nothing changes: an input outside the enum, a room not
  below the rooms' count, or an instant that is not finite.

`ctl_loop_confirm` stores what the glue knows of the thermostat state the pump reports; the
router reads it at the next decision tick.

## The two ticks (`ctl_loop_tick`)

The caller calls `ctl_loop_tick` often, every few hundred milliseconds or on every pass of its
main loop, with the current instant. An instant that is not finite changes nothing. Otherwise:

1. **The integration tick**, when the instant has reached `next_advance`: `ctl_advance` to the
   instant, and the next one `integration_interval_s` (10 s) later. This keeps the energy integral
   current between decisions.
2. **The decision tick**, when the instant has reached `next_evaluate`, in this order:
   1. the room average at the instant (`ctl_rooms_average_at`);
   2. with an average, `ctl_observe` of `T_INDOOR` with it at the instant; without one, the core
      gets no indoor reading and keeps the one it held, until that one goes stale;
   3. `ctl_evaluate` at the instant;
   4. `ctl_router_step` with the decision's demand and the pump's state (see *The defaults*);
   5. the next decision tick `tick_interval_s` (60 s) later;
   6. the record: the instant, the decision, the room average, the commands to send and the
      router's status after the step. Only this tick returns true.

Both ticks are scheduled from the instant they ran, not from the one they were due, so a caller
that is late shifts the ticks rather than catching up with a burst.

## The record's text (`ctl_loop_format_record`)

One line, its fields separated by single spaces:

| Field | Content |
|---|---|
| `t=` | the instant, 1 decimal |
| `demand=`, `reason=`, `phase=` | the names of the core's enums |
| `E=` | the energy deviation in Wh, 1 decimal |
| `pnet=` | the net power in W, 0 decimals, or `-` when the core had no inputs |
| `indoor=`, `outdoor=` | the temperatures the core used, 2 decimals, or `-` without inputs |
| `produced=`, `solar=` | the powers the core used in W, 0 decimals, or `-` without inputs |
| `rooms=` | the fresh rooms' count, `:`, their weight with 1 decimal, `/`, the total weight with 1 decimal |
| `router=` | `IDLE`, `WAITING` or `FAULT` |
| `cmd=` | the commands as `name:value` joined by `,`, or `-` when there are none |

An example, a decision that turns the heating on with the heat/cool polarity set:

```
t=600.0 demand=HEATING reason=NORMAL phase=RUNNING E=-1523.4 pnet=-2735 indoor=22.46 outdoor=5.00 produced=3200 solar=20 rooms=3:3.0/4.0 router=WAITING cmd=SetExternalThermostat1State:2,SetHeatCoolMode:0
```

Numbers are written by `ctl_format_fixed` (`ctl_format.h`) with integer arithmetic, never by
`printf` of a float: fixed decimals, rounded half away from zero, no sign before a result that
rounds to zero, `nan`, `inf` and `-inf` for the values that are no number, and a refusal for a
magnitude of 1e12 or more.

A record that does not fit the buffer, or that holds a number the fixed format refuses, is
refused whole, never cut: the function returns false and leaves an empty text.
`CTL_LOOP_TEXT_MAX` (365 bytes) holds every record the function can write: the longest line has
every number at the fixed format's widest (just below 1e12, rounded up to thirteen digits, with a
sign), the longest name of every enum and both commands filling their name and value fields. A
buffer of that size is therefore refused only for a number the fixed format refuses.

## The clock (`ctl_clock.h`)

The core needs elapsed time that never jumps back (`firmware-design.md` §5), but the boards'
`millis()` is a 32-bit counter of milliseconds and wraps to 0 every 2^32 ms, about 49.7 days. A
board that runs for months would see time go backwards, which the core refuses as no time at
all. `ctl_clock_widen` widens the counter to 64 bits: its first call takes the counter as it is,
and each later call whose counter lies below the previous one counts a wrap and adds 2^32 to the
high part. The result is the high part plus the counter. Two wraps between calls would count as
one, so the glue calls it at least once per wrap; on every pass of its main loop is far more
often than that. The glue divides the result by 1000.0 to get the loop's seconds.

## The defaults (`ctl_loop_config_defaults`)

- **Cooling is masked.** `allowed` holds only `HEATING` and `NONE`. A decision that would cool
  comes out as `NONE` with the reason `MASKED`, and the core clamps the energy in the cooling
  direction to the hysteresis, so that nothing banks up. Cooling is switched on only by the owner.
- **Landing is not assumed.** `assume_landed` is false: the router waits for the pump's state that
  the glue hands in with `ctl_loop_confirm`, and repeats and then faults a command that does not
  land (`router.md`). A shadow, which sends no command to a pump, sets it to true: every command
  then counts as landed at the next decision tick, so that its router does not repeat and fault on
  commands nobody sent.
- The core's, the rooms' and the router's own defaults (`core.md`, `adapters.md`, `router.md`).
