# Pump values from MQTT, for a board that shadows another HeishaMon

## Goal

Inside the firmware, the loop takes the outdoor temperature, the produced heat, the valve and the defrost flag from the board's own decoded frames. A board without a pump, such as the owner's spare D1 mini, therefore never gets them, and its loop waits for inputs forever. This feature lets such a board take the four pump values from MQTT instead, from the topics another HeishaMon publishes. The full firmware can then run beside the production installation, decide live, publish everything, and command nothing: the shadow inside HeishaMon itself, with no wiring. It is for the owner, who wants to watch the real firmware on real data before any of it is connected to the pump. Done when the story is merged and `make check` is green.

## Scope

- Four settings, `outdoor_topic`, `heat_topic`, `valve_topic` and `defrost_topic`, empty by default.
- The glue subscribes to the ones that are set and hands their messages to the loop.
- While any of them is set, the glue hands no pump value from the board's own frames to the loop, so the two sources never mix.

## Out of scope

- Any change to the loop, the adapters or the router.
- Commands: a board shadowing another sends nothing unless its owner enables commands, as before.

## Stories

- S0001: The pump values from MQTT

## The human's changes

None.

## Decisions taken for the human

1. One source at a time: as soon as one pump topic is set, the frames hand no pump values, rather than letting the two sources race.
2. The settings are empty by default, so a board on a pump behaves exactly as before.
