# The router

The router stands between the core and the pump. It turns the core's demand into the commands of
HeishaMon's optional PCB emulation, and it decides when to
send them. It lives in `HeishaMon/src/control/route/` as plain C99: like the core and the
adapters it takes values and returns values. The glue hands it the demand, the thermostat state
the pump reports and an instant, and sends the commands it returns; the router sends nothing
itself.

## The mapping (`ctl_router_state_for`)

| Demand | `SetExternalThermostat1State` | `SetHeatCoolMode` |
|---|---|---|
| `HEATING` | `2` | `heat_cool_heat_value`, when it is set |
| `COOLING` | `1` | `1 - heat_cool_heat_value`, when it is set |
| `NONE` | `0` | never sent |
| a value outside the enum | `0` | never sent |

A value outside the enum is read as no demand, so that a corrupt demand never heats or cools.

## The polarity setting

`heat_cool_heat_value` is the value of `SetHeatCoolMode` that means heat: 0 or 1, or -1 for
unset. It has no default because nobody knows yet which value the emulated frame takes for heat
on this pump, and a wrong guess would cool when the core asks for heat. While it is unset (the
default), the router sends only the thermostat state and leaves the pump's heat/cool mode as it
is; the owner sets it once the pump has shown which value is which. A step that sends no demand
never sends the mode either, because "no demand" has no direction.

## A step (`ctl_router_step`)

A step empties the command list, then applies six rules in this order:

1. **A `now` that is not finite** changes nothing and sends nothing. Nor does a configuration
   that `ctl_router_config_valid` refuses.
2. **The wanted state** becomes the mapping of the demand.
3. **The state has landed** when the pump's state is known and equals the sent state. Then the
   retries are reset and the status is `IDLE`, unless the wanted state already differs from the
   sent one: then the router is still waiting to send the change, and the status stays.
4. **A new state** (the wanted state differs from the sent one, or nothing has been sent) is sent
   when nothing has been sent yet or at least `min_change_interval_s` has passed since the last
   new state. Otherwise nothing is sent and the status stays. A send writes the thermostat state
   and, by the table above, the heat/cool mode, and leaves the status `WAITING`.
5. **A repeat.** Otherwise, while the status is `WAITING`, the state has not landed and
   `retry_interval_s` has passed since the last send, the router sends the same commands again,
   up to `max_retries` times; when the repeats are used up the status becomes `FAULT` and nothing
   is sent.
6. **A fault** stays until the state lands (rule 3) or a new state is sent (rule 4). It sends
   nothing by itself.

A fresh router (`ctl_router_init`) wants no demand and has sent nothing, so its first step sends
`0` at once: a boot starts at no demand.

The pacing counts from the last new state, not from a repeat, so that repeats of a dropped command
do not hold back the next change. A pump whose state is not known (`state_known` false) never
confirms, so the router repeats and faults as if the command had been dropped.

Only a send waits for its confirmation. Once a state has landed the status is `IDLE`, and a report
that is lost or differs later sends nothing: a repeat then would write the pump's EEPROM again for
a state the pump has already shown. A repeat sends exactly the commands of the send it repeats,
the heat/cool mode included.

## The configuration (`ctl_router_config`)

| Setting | Default | Accepted | Why this default |
|---|---|---|---|
| `min_change_interval_s` | 300.0 | finite, 0 or more | Every write of a pump setting wears the pump's EEPROM, and upstream asks for a few changes per hour per setting at most. Five minutes caps the router at twelve changes an hour even if the core flips, and the core's hysteresis already paces it far below that. |
| `retry_interval_s` | 120.0 | finite, 0 or more | The emulated frame goes out every few seconds and the pump's state comes back with the next decoded frame, so two minutes is ample for a command to land; a repeat sooner would count as a second write of the same EEPROM setting. |
| `max_retries` | 3 | 0 to 10 | A dropped command is rare; three repeats over six minutes ride out a busy link without writing the EEPROM again and again when something is truly wrong, which the fault then reports. |
| `heat_cool_heat_value` | -1 (unset) | -1, 0 or 1 | See *The polarity setting*. |

## A worked sequence

With the defaults, the core asks for heat on a fresh router at 0.0 and the pump keeps reporting 0:

| `now` | Sent | `retries` | Status |
|---|---|---|---|
| 0.0 | `2` | 0 | `WAITING` |
| 119.0 | — | 0 | `WAITING` |
| 120.0 | `2` (repeat) | 1 | `WAITING` |
| 239.0 | — | 1 | `WAITING` |
| 240.0 | `2` (repeat) | 2 | `WAITING` |
| 360.0 | `2` (repeat) | 3 | `WAITING` |
| 480.0 | — | 3 | `FAULT` |
| 600.0 | — | 3 | `FAULT` |

From there, a report of 2 at 610.0 lands the state: `IDLE`, nothing sent. A demand of `NONE` at
610.0 instead sends `0` with the status `WAITING`, since 610.0 is at least 300.0 after the last
new state at 0.0.
