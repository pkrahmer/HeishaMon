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
