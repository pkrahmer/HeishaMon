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

The three consequences of `firmware-design.md` §3.1:

1. The room timeout, not the core's 600 s limit on `T_INDOOR`, is the real staleness limit of the
   indoor signal, because the average keeps being emitted from held room values until the half
   rule fails.
2. Changing the list of rooms or their weights moves what the setpoint means, since the setpoint
   and the trim were fitted against the plain mean of the archived sensors.
3. A room leaving or joining the average makes it jump by up to several tenths of a kelvin, which
   the trim turns into a few hundred watts.
