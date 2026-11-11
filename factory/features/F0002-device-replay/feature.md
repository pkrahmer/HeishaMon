# The core proven on a board: the golden traces replayed on a D1 mini

## Goal

The C core passed every golden trace on the host. On a board a different compiler computes its doubles in software, because neither the ESP8266 nor the ESP32-S3 has a hardware unit for doubles. This feature proves that the core decides there exactly as on the host: a test sketch on a D1 mini (ESP8266) serves the core over the USB serial line, and a script on the PC feeds it the thirteen golden traces and compares every recorded value. It also measures what the core costs on the weakest board: its memory and its time per call. It is for the owner, who wants to know this before anything is wired into the firmware. Done when the story below is merged, `make check` is green, and the owner's run on the board reports 0 mismatches.

## Scope

- A line protocol in C under `HeishaMon/src/control/wire/`: one request line in, one reply line out, doubles as their exact bit patterns.
- The test sketch `tests/control/device/replay/replay.ino` for the D1 mini, and its host twin `tests/control/device/host_server.c`.
- The script `tests/control/device/replay.py`, which feeds traces to either and compares.
- `docs/control/device-test.md`: what the test proves, how to flash the board and run it.

## Out of scope

- Any change to the core: a mismatch on the board is a question for the owner, not a fix in this feature.
- The ESP32-S3, and any part of the firmware: no hook, no upstream file.
- Flashing and running the board: no stage has one. The owner, or the session in the owner's seat, runs `make device-replay` on the PC the board is plugged into.

## Stories

- S0001: The device replay: a line protocol, a test sketch and a replay script

## The human's changes

Made before S0001, in the pull request that adds this feature:

- `factory/stage.Containerfile` adds arduino-cli 1.1.1 (checksum pinned) and the ESP8266 core 3.1.2, writable by the stage identities.
- The `Makefile` adds `make device` to `check` (it compiles the sketch when it exists), runs the host replay in `make test` when `host_server.c` exists, and adds `make device-replay PORT=<port>` for the board.
- `CLAUDE.md` describes the device test and the two targets.

## Decisions taken for the human

1. The board is the owner's D1 mini with an ESP-12F (ESP8266), not the ESP32-S3 the design names. Both compute doubles in software, so a pass here is strong evidence for the S3; the S3 is checked again when its firmware is built.
2. Doubles cross the serial line as 16 hexadecimal digits of their bit pattern, never as decimal text: the board's `printf` and `strtod` are not trusted to round exactly, and they are not what is under test.
3. The host twin is built and fed two traces in every `make test`, so the protocol and the script are proven without a board.
4. The serial line runs at 921,600 baud with up to 16 requests in flight, so the 332,239 calls take minutes, not hours.
