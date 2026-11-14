# The adapters

The adapters stand between the outside world and the core: they take what the sources publish
over MQTT (the CCU3's room temperatures, HeishaMon's own values) and turn it into the numbers the
core's signals take, refusing what is malformed or implausible so that the core sees such a value
as missing. Like the core they are pure: they live in `HeishaMon/src/control/adapt/` as plain C99
that takes values and returns values, with no input or output, no clock and no allocation, and
they compile and are tested on the host. The code that subscribes to MQTT and calls them is not
part of them.

## The payload parser (`ctl_payload.h`)

`ctl_payload_parse` reads a payload of at most `CTL_PAYLOAD_MAX` (256) bytes, which need no
terminating zero; nothing after the given length is read. Spaces, tabs, `\r` and `\n` before and
after the payload are skipped. It accepts exactly two shapes:

| Shape | Example | Gives |
|---|---|---|
| A bare number: optional `-`, digits, optional `.` and digits, optional exponent (`e` or `E`, optional sign, digits) | `3200` | 3200.0, no timestamp |
| A flat JSON object with a `"v"` or `"value"` member and an optional `"ts"` | `{"ts":1794430802765,"v":21.1,"s":0}` | 21.1, timestamp 1794430802765 ms |

In the object, spaces may stand between the tokens; keys are strings in double quotes; the value
member is a number in the bare form; `"ts"` is 1 to 18 digits; any other member is skipped when it
is a number, `true`, `false`, `null` or a string.

Refused, with `*payload` left as it was:

- a length above `CTL_PAYLOAD_MAX`, the empty text and text of spaces only;
- a number with a leading `+` or point, a trailing point, an exponent without digits, `nan`,
  `inf`, hexadecimal, or a value that is not finite as a double (`1e999`);
- anything after the number or the closing `}` but spaces, such as `5 6` or `{"v":1} x`;
- an object without a value member, with both `"v"` and `"value"`, with a key twice, with a nested
  object or array, with a trailing comma, with an unquoted key, or with a value member that is a
  string;
- a backslash anywhere in a key or a string: escapes are refused, not decoded;
- a timestamp with a sign, a point, or more than 18 digits.

## The bounds (`ctl_bounds.h`)

`ctl_bounds_accept` is true when the value is finite and within its bound's range, both ends
included. It is false for NaN, the infinities, values outside, and a bound that is no member of
the enum.

| Bound | Low | High | Unit |
|---|---|---|---|
| `CTL_BOUND_TEMPERATURE` | -60.0 | 60.0 | °C |
| `CTL_BOUND_HEAT` | 0.0 | 30000.0 | W |
| `CTL_BOUND_PV` | 0.0 | 100000.0 | W |
| `CTL_BOUND_WEIGHT` | 0.0 | 1000.0 | |

## The room average (`ctl_rooms.h`)

The house has several room thermostats; the core has one indoor temperature. `ctl_rooms` keeps
each room's last accepted reading, and `ctl_rooms_average_at` turns them into the weighted mean
over the rooms that are fresh, `Σ wᵢ·Tᵢ / Σ wᵢ`, summed in room order. The glue offers each room's
payload with `ctl_rooms_offer` as it arrives and asks for the average on its own 60 s clock; when
there is one, it becomes one `T_INDOOR` reading for the core. Instants are seconds of the core's
monotonic clock, handed in by the caller.

An offered payload is parsed (`ctl_payload_parse`) and its value checked against
`CTL_BOUND_TEMPERATURE`; a refused payload, an implausible value or an arrival instant that is not
finite leaves the rooms as they were. A room is fresh while `now - t` is at most the room timeout,
`t` being the arrival instant of its last accepted value.

**The replay rule.** A payload with a timestamp is refused as replayed when the room's last
accepted payload had a timestamp at least as large. A source that reconnects, or a retained
message the broker delivers again, would otherwise make an old reading look new, and the room
would stay in the average long after its thermostat went quiet. A payload without a timestamp
carries nothing to compare, so it is always taken, and it clears the room's timestamp: the next
timestamped payload is taken whatever its timestamp.

**The half rule.** The average is emitted only while the fresh rooms carry strictly more than
half of the total configured weight; otherwise nothing is emitted, and the core's own staleness
handling of `T_INDOOR` takes over. With four rooms of weight 1.0, three fresh rooms (3.0 of 4.0)
give an average over those three; two fresh rooms (2.0 of 4.0) give none. A room of weight 0 is
fresh or not like any other and is counted in `fresh_count`, but it moves neither the weights nor
the mean: a sensor to watch before it is trusted.

**Defaults** (`ctl_rooms_config_defaults`): every weight 1.0, room timeout 1800 s. A
configuration is valid with 1 to `CTL_ROOMS_MAX` (16) rooms, each weight accepted by
`CTL_BOUND_WEIGHT` (0 to 1000), a weight sum above 0, and a finite timeout above 0;
`ctl_rooms_average_at` gives no average for a configuration that is not valid.

The three consequences of averaging the rooms:

1. The room timeout, not the core's 600 s limit on `T_INDOOR`, is the real staleness limit of the
   indoor signal, because the average keeps being emitted from held room values until the half
   rule fails.
2. Changing the list of rooms or their weights moves what the setpoint means, since the setpoint
   and the trim were fitted against the plain mean of the archived sensors.
3. A room leaving or joining the average makes it jump by up to several tenths of a kelvin, which
   the trim turns into a few hundred watts.

## The pump signals and the solar gain (`ctl_pump.h`)

HeishaMon publishes the outdoor temperature (`Outside_Temp`), the produced heat
(`Heat_Power_Production`, in W), the position of the three-way valve (`ThreeWay_Valve_State`, 1
while the pump heats the hot-water tank) and the defrost flag (`Defrosting_State`); the PV system
publishes its DC power. Each function takes one payload and the instant it arrived, and returns
true when it wrote one `ctl_reading` (signal, value, instant) for the core. It returns false and
leaves the reading, and the pump's state, as they were when the payload does not parse, the value
is outside its bound or not one of the values the function names, or the instant is not finite.

| Function | Payload | Bound or values | Reading |
|---|---|---|---|
| `ctl_pump_outdoor` | `Outside_Temp` | `CTL_BOUND_TEMPERATURE` | `T_OUTDOOR`, the value |
| `ctl_pump_heat` | `Heat_Power_Production` | `CTL_BOUND_HEAT` | `P_PRODUCED`, the value, or 0.0 while masked |
| `ctl_pump_valve` | `ThreeWay_Valve_State` | exactly 0 or 1 | `P_PRODUCED` under the new mask, when the mask switches and a heat is stored |
| `ctl_pump_defrost` | `Defrosting_State` | exactly 0 or 1 | `P_PRODUCED` under the new mask, when the mask switches and a heat is stored |
| `ctl_solar` | PV DC power | `CTL_BOUND_PV`; the gain finite and ≥ 0; the product finite | `P_SOLAR`, `pv * solar_gain_factor` |

**The mask rule.** The produced heat is reported as 0 W while the valve is on the hot-water tank
or the pump defrosts, because that heat does not enter the rooms.
`ctl_pump` keeps the last accepted heat before the mask, so that the end of a hot-water run or a
defrost gives the stored heat back at once, not at the next power report. Only a switch of the
mask itself gives a reading; a flag that changes while the other one still masks gives none. A
flag also changes while no heat is stored, and the first heat after it is masked. For example,
on one pump:

| Input | Instant | Reading | Flags after it |
|---|---|---|---|
| heat `3200` | 0.0 | `P_PRODUCED` 3200.0 | — |
| defrost `1` | 10.0 | `P_PRODUCED` 0.0 | defrost |
| valve `1` | 20.0 | none: already masked | defrost, hot water |
| defrost `0` | 30.0 | none: the valve still masks | hot water |
| valve `0` | 40.0 | `P_PRODUCED` 3200.0 | — |

**DC, not AC.** The solar gain is the PV array's DC power times `solar_gain_factor`, because the
inverter's AC output is what remains after the battery has taken its share and so follows dispatch
rather than sunshine. The firmware does not feed this adapter (`firmware.md`).
