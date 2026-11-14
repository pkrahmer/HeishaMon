# Remove the device replay

## Goal

The device replay proved that the C core computes bit for bit the same on a D1 mini (ESP8266) as on the host: a line protocol (`HeishaMon/src/control/wire/`), a sketch that serves the core over the serial line, a host server that serves it over standard input and output, and `replay.py`, which feeds a golden trace to either and compares. That proof was needed before the core ran on the owner's production board. It now has: the production board has run the core since 2026-11-13, and the build forbids the floating-point contractions that could make the two differ. The owner decided to remove the replay, so that the fork carries only what the firmware and its host tests need. The human removed its targets from the `Makefile` before this story. Done when the story is merged, `make check` is green, and the docs describe only what remains.

## Scope

- The line protocol `HeishaMon/src/control/wire/`, everything under `tests/control/device/`, the host tests of the protocol, and `docs/control/device-test.md`.

## Out of scope

- The core and the golden traces with their host test (`tests/control/test_traces.c`): they stay.
- The `Makefile`: the human changed it before this story.
- Upstream files.

## Stories

- S0001: Remove the device replay

## The human's changes

Before S0001, in the `Makefile`: `make check` no longer runs `make device`; the targets `device` and `device-replay` are removed; `make test` no longer builds the host server, runs `replay.py` or the scripts `tests/control/device/test_*.py`. Done by the human in commit "Makefile: drop the device replay".

## Decisions taken for the human

1. The host test of the golden traces stays; it is the proof that the core matches the Python reference.
2. Removing a test that only tested removed code is allowed (`tests/control/test_wire.c`, `tests/control/test_frame.c` and anything else that tests only the protocol).
