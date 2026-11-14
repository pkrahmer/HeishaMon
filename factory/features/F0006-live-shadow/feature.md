# The live shadow: the whole loop on the D1, fed from the house, commanding nothing

## Goal

The core, the adapters and the router are proven one by one. This feature runs them together, live, beside the real installation, without any path to the pump. A pure C loop ties them together on the design's two ticks: advance every 10 s, decide every 60 s (`docs/control/firmware-design.md` §5). A shadow sketch runs that loop on the owner's spare D1 mini with the board's own clock. A bridge on the PC subscribes read-only to the owner's MQTT broker, forwards the room temperatures and the pump values to the board over USB, and writes every decision the board makes to a file with the wall time. In the morning the owner sees what the loop would have done in the house overnight, and how often each room thermostat really reports. Done when both stories are merged, `make check` is green, and the shadow runs on the D1.

## Scope

- `HeishaMon/src/control/loop/`: the loop (inputs, ticks, the decision record, its text form) and the widening of the board's 32-bit millisecond counter to a clock that does not wrap.
- `tests/control/device/shadow/shadow.ino`, its host twin `tests/control/device/shadow_host.c`, and the bridge `tests/control/device/shadow.py` with an example configuration.
- `docs/control/shadow.md`.

## Out of scope

- Any path to the pump: the board is not wired to it, and the router's commands are only logged.
- Writing to the broker: the bridge only subscribes.
- The firmware hooks: a later feature calls the same loop from inside HeishaMon.
- The owner's broker address and topics in the repository: they live in a configuration file outside it, beside the example.

## Stories

The build order; start one at a time, the next when the one before is archived in `done/`.

- S0001: The loop
- S0002: The shadow sketch, its host twin and the bridge

## The human's changes

Made before S0001, by the session in the owner's seat: `make device` compiles every sketch folder under `tests/control/device/` (each folder that holds a `.ino` file of its own name), not only `replay/`, so that S0002's sketch is compiled by `make check`.

## Decisions taken for the human

1. Cooling is masked off: the loop allows heating and no demand only, as the owner chose for the first winter (§9, item 8).
2. In the shadow the router's commands land at once (the loop's confirmation is "assumed landed"), because no pump answers; the shadow shows what would be sent, not whether it would land.
3. The records are text with fixed decimals written by the loop's own formatter, not by `printf` with floats, because the board's `printf` is not trusted to format floats.
4. The rooms are the CCU3 wall thermostats the broker showed on 2026-11-11, each with weight 1. That is not the legacy's five-sensor mean, so the setpoint means something slightly different (`docs/control/firmware-design.md` §3.1, consequence 2); the shadow shows it, it does not tune it.
5. The room timeout in the shadow is 3 hours instead of the design's 30 minutes, because the broker showed room values up to about 90 minutes old; the bridge logs every room report, so the morning shows the real cadence and the right timeout.
