# Remove the device replay

## Description

**Why.** The device replay proved that the core computes on a D1 mini exactly as on the host. The production board has run the core since 2026-11-13, and the owner decided to remove the replay. The human has already removed its targets from the `Makefile`, so nothing builds or runs it any more.

**What goes.** The line protocol `HeishaMon/src/control/wire/` (all its files); everything under `tests/control/device/` (the replay sketch, `host_server.c`, `replay.py`, the `test_*.py` scripts and any helper only they use); the host tests that test only the protocol (`tests/control/test_wire.c`, `tests/control/test_frame.c`, and any other such test the builder finds and names); `docs/control/device-test.md`; and every link to these from another document of the fork, which is removed or pointed to `core.md` or `golden-traces.md`. The human has already removed the device test and `make device` from `CLAUDE.md`.

**What stays.** The core, the golden traces and `tests/control/test_traces.c`, every other host test, and the firmware's behaviour: nothing under `HeishaMon/src/control/` outside `wire/` changes, unless it only served the protocol, which the result names.

## Interface

- Removed: `HeishaMon/src/control/wire/`, `tests/control/device/`, the protocol's host tests, `docs/control/device-test.md`.
- The leave for tests another story wrote: `tests/control/test_wire.c`, `tests/control/test_frame.c` and any other test that tests only the protocol may be deleted. No other test changes.
- No upstream file changes, no hook line changes, and no change to the `Makefile` or `CLAUDE.md`.

## Acceptance criteria

1. `git ls-files HeishaMon/src/control/wire tests/control/device docs/control/device-test.md` prints nothing.
2. `git grep -n -E "ctl_wire|ctl_frame|replay\.py|host_server|device-test\.md|tests/control/device" -- HeishaMon tests docs/control ':!docs/control/decisions.md'` prints nothing.
3. Every golden trace replays bit for bit in `tests/control/test_traces.c`, as before.
4. `make firmware` builds both boards; the D1 mini's global RAM and flash are no larger than on `develop`.
5. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
6. `make check` stays green.
7. docs: `docs/control/golden-traces.md` and any document that described the replay say only what remains; `docs/control/decisions.md` gets one entry: the device replay removed, because the production board runs the core and the host traces with `-ffp-contract=off` keep the proof; rejected: keeping it as an optional tool (nothing would build or run it, so it would rot).

## Smoke test

```sh
make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l
```

Expect: `1`

## Journal
