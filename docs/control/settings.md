# The settings

The settings are what the owner sets to fit the control loop to the house, the pump and the
board. Each setting is set on the board's control page, `/control`, or over MQTT by a message to
`<base>/control/set/<name>` whose payload is the value, such as
`panasonic_heat_pump/control/set/hysteresis_wh` with `1500`; the value in force comes back,
retained, on `<base>/control/config/<name>`. A value that is refused keeps the old one. A number is
a bare number whose magnitude is below 10¹², with at most three decimals; a topic is empty or 1 to
96 printable ASCII characters (0x21 to 0x7e) other than space, `+` and `#`.
`firmware.md` (*The settings*) gives the rules in full.

| Setting | Default | Allowed | What it does |
|---|---|---|---|
| `enabled` | `0` | `0` or `1` | Send commands to the pump: 0 = only watch and decide, 1 = control the pump. |
| `cooling` | `0` | `0` or `1` | Allow cooling demand: 0 = heating only, 1 = heating and cooling. |
| `rooms` | `0` | whole, 0 to 16 | Number of room sensors in the indoor average. On the control page, save the count first; the room fields then appear. |
| `room.<i>.topic` | empty | a topic; one per room `i` below `rooms` | MQTT topic of room `i`'s temperature. Empty: the room never receives a value. |
| `room.<i>.weight` | `1` | 0 to 1000; one per room `i` below `rooms`; the weights of all rooms together above 0 | Weight of room `i` in the indoor average; 0 = watched, not counted. |
| `room_timeout_s` | `1800` | s, above 0 | Seconds after which a room's last value counts as too old. |
| `pv_topic` | empty | a topic | MQTT topic of the PV power in W, for the solar gains. Empty: no PV. |
| `desired_indoor_c` | `22` | °C, 5 to 30 | Target for the indoor average. |
| `transmission_w_per_k` | `155` | W/K, above 0 | The house's heat loss per kelvin between inside and outside. |
| `adjustment_w_per_k` | `805` | W/K, 0 or more | Extra power per kelvin the indoor average is off its target. |
| `hysteresis_wh` | `1500` | Wh, above 0 | The energy balance at which the pump is switched on: heating at minus this, cooling at plus this; it is switched off when the balance is back at 0. |
| `trim_limit_k` | `3` | K, above 0, or `none` | The largest deviation from the target that the adjustment acts on. `none`: no limit. |
| `integral_w_per_kh` | `0` | W/(K·h), 0 to 200 | How fast the integral term learns the power the adjustment leaves missing: watts added per kelvin of deviation and hour. 0 = off; a learned integral then stays as it is. |
| `integral_limit_w` | `300` | W, above 0 up to 2000 | The largest integral term, either way. Lowering it below the term's size clamps the term at once. |
| `solar_gain_factor` | `0.02` | 0 or more | Share of the PV power counted as solar heat entering the house. |
| `internal_w` | `550` | W, any number | Heat from people and devices inside. |
| `heat_cool_heat_value` | `-1` | `-1`, `0` or `1` | The SetHeatCoolMode value that means heat; -1 never sends it. Used only without Smart Grid. |
| `min_change_interval_s` | `300` | s, 0 or more | Shortest time between two switches of the pump. |
| `retry_interval_s` | `120` | s, 0 or more | Seconds before a command that was not confirmed is sent again. |
| `max_retries` | `3` | whole, 0 to 10 | Repeats of an unconfirmed command before the router reports a fault. |
| `smart_grid` | `0` | `0` or `1` | How the pump is switched: 1 = Smart Grid mode (0 for heating or cooling demand, 1 = Capacity 1 for no demand), 0 = the external thermostat input. |

## The integral term

The adjustment alone leaves the house steadily off its target by whatever the model gets wrong,
divided by `adjustment_w_per_k`. The integral term learns that missing power slowly and removes
the offset; `core.md` (*Controller*, *The five functions*) says when it learns. It is off by
default. A start value of `25` for `integral_w_per_kh` at an `adjustment_w_per_k` of `600` is a
suggestion, not a default: it takes some hours to learn an offset of a few tenths of a kelvin,
slowly enough that it does not swing against the house's own inertia.

A message to `<base>/control/set/integral_reset`, with any payload, sets the term to 0. It is a
command, not a setting: nothing is stored in `/control.json` and nothing comes back on
`<base>/control/config/`. The term itself is published on `<base>/control/status/p_integral_w`
(`-` while the inputs are missing), and the energy it has booked since the start on
`<base>/control/status/integral_wh`.

The learned term survives a restart: the board keeps it in `/control_integral.txt`, written at
most every 6 hours and only after it moved by 10 W, and loads it at boot (`firmware.md`, *The
integral file*).

## Running a board on the pump with Smart Grid mode

The owner's production board is wired to the pump and switches it by Smart Grid mode:

- `smart_grid` `1`;
- `enabled` `1`, once the rooms, `pv_topic` and the tuning are set.

HeishaMon's optional PCB emulation must be on and listen-only mode off, or no command reaches the
pump (`firmware.md`, *What reaches the pump*).

Smart Grid value 1 stops heating only on a pump whose Capacity 1 the installer set to a curve
that never heats. On any other pump, value 1 does not stop heating, so the loop's "no demand"
leaves the pump running.
