# Frames from another HeishaMon's MQTT log

## Goal

A board without a pump can take the four pump values from MQTT (F0009), but that skips HeishaMon's own path: the received frame, its checksum, the decoder and the control loop's frame hook. This feature feeds that path with real frames, with no wiring and no PC. A production HeishaMon with hex dumps and "log to MQTT" on publishes every frame it receives from the pump as log lines. A dev board subscribes to that log topic, rebuilds each main data frame from its lines, checks the checksum as HeishaMon does, and hands the frame to HeishaMon's own `decode_heatpump_data` and then to the control loop's frame hook. The dev board then behaves as if it were wired to the pump: it publishes the decoded values under its own base, and its loop takes the pump values from its own decoder. It is for the owner, who wants the firmware tested end to end on the house's real frames before it goes near the pump. Done when the story is merged, `make check` is green, and the dev board decodes production's frames.

## Scope

- A setting `frame_log_topic`, empty by default.
- A pure C parser that rebuilds frames from HeishaMon's log lines.
- The glue: it subscribes to the topic, feeds its messages to the parser, and decodes each complete main data frame through upstream's decoder and the frame hook.

## Out of scope

- The optional PCB answers (20-byte frames) and the extra data block: only the main data block (203 bytes, byte 3 is `0x10`).
- Any change to the production board; its settings are the owner's.

## Stories

- S0001: The frames from a log topic

## The human's changes

None in the repository. On the production HeishaMon the owner switches on "log to MQTT" (hex dumps are already on).

## Decisions taken for the human

1. The glue calls upstream's `decode_heatpump_data` itself, with HeishaMon's globals, instead of a new hook line: the decoder is a public function of `decode.h`, and no upstream file changes.
2. The four pump topics of F0009 still win: while any is set, the frame hook hands nothing, so the owner clears them to use the frames.
