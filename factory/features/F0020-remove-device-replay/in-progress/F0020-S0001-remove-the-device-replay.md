---
stage: approval
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: 35
comments_read: []
handed_over: approval
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-14T10:51:44.852008Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-14T10:51:44.858089Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-14T10:58:00.290293Z'
  stage: doing
  run: 15e61d2a59c14208bcbac8b641eadee0
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-14T10:58:05.594947Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-14T10:58:42.262083Z'
  stage: review
  run: 6ff65b2f99e849f0b8c0e7d7e0ccd65b
---

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

1. 2026-11-14 human: created.
2. factory: started doing as heishamon.
3. builder: Removed the device replay. Nothing else under the fork's code changed.

   **Deleted:**
   - `HeishaMon/src/control/wire/`: `ctl_wire.c` and `ctl_wire.h`, `ctl_frame.c` and `ctl_frame.h`.
   - `tests/control/device/`: `replay/replay.ino`, `host_server.c`, `replay.py`, `test_replay.py` and `garbling_twin.py`.
   - The protocol's host tests: `tests/control/test_wire.c` and `tests/control/test_frame.c`. I searched for other protocol-only tests and found none.
   - `docs/control/device-test.md`.

   **Documents:**
   - `docs/control/core.md`: the last paragraph of the trace runner's section linked to `device-test.md`. It now says that the host build forbids fused multiply-add, that a replay on a D1 mini once showed the board computes alike, and that the production board has run the core since.
   - `docs/control/decisions.md`: a new entry for 2026-11-14 with the decision, the reason and the rejected option the story asks for.
   - `docs/control/golden-traces.md` needed no change: it never described the device replay.
   - The older entries in `decisions.md` that mention the device test stay as they are, because the log is history. Criterion 2's grep leaves that file out as well.

   **Decisions:**
   - Nothing outside `wire/` served only the protocol. Every core function that `ctl_wire.c` called (`ctl_advance`, `ctl_observe`, `ctl_evaluate`, `ctl_set_allowed`, `ctl_state_init`, `ctl_config_defaults`, `ctl_config_validate`) has other callers in the code and tests.
   - The faulty traces under `tests/control/data/bad/` stay, because `test_traces.c` replays them.
   - I wrote no new test program, because this story only removes things. The criteria are the story's own commands, and I ran each one (results below).
   - The deletions are left as staged `git rm` changes in the working tree. Nothing is committed.

   **Results:**
   - Criteria 1 and 2: both commands print nothing.
   - Criterion 3: `traces: 13 files, 332239 calls, 0 mismatches`.
   - Criterion 4: the D1 mini's sizes are the same before and after the removal: RAM 48244 / 80192 bytes and flash 757144 / 1048576 bytes. The baseline came from `make firmware` before any deletion, on a tree that differed from `develop` only by the work item file. The ESP32-S3 is also unchanged: 1705151 bytes of flash and 67368 bytes of global RAM. The linker had already dropped the unused protocol.
   - Criterion 5: `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` prints nothing.
   - Criterion 6: `make check` is green (`control: 33 test programs passed`, `check: green`).
   - Smoke test: `make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l` prints `1`, as expected.

   **Noticed, not touched:**
   - The firmware build does not pass `-ffp-contract=off` explicitly; only the host `CFLAGS` in the `Makefile` set it. So the `core.md` text claims nothing about the board's flags.
   - Upstream's rules suite prints compiler warnings in `rules.cpp` and `settimer.cpp`. They are upstream's files and the suite still passes.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: no findings

   What I checked, in the order of the checklist:

   1. **Acceptance criteria.** I ran criteria 1, 2 and 5 against the branch, and all three print nothing. Criterion 3 rests on `tests/control/test_traces.c`, which the diff leaves alone, along with `trace.c`/`trace.h` and every trace under `tests/control/data/` and `data/bad/`. Criterion 4's sizes come from the builder's `make firmware` runs, recorded in the journal; I did not rebuild. Criterion 6: the journal records `check: green` after the builder. This story only removes code, so the criteria are its own commands. It adds no new rule that would need a test of its failing side.
   2. **Architecture.** `git diff --name-status develop...HEAD` lists only deletions under `HeishaMon/src/control/wire/`, `tests/control/device/` and `tests/control/test_{wire,frame}.c`, the deletion of `docs/control/device-test.md`, and edits to `docs/control/core.md` and `docs/control/decisions.md`. No upstream file, hook line, `Makefile` or `CLAUDE.md` changed. Nothing under `HeishaMon/src/control/` outside `wire/` changed.
   3. **Embedded safety.** No code was added. The only code changes are deletions.
   4. **Quality.** Nothing is left orphaned. The shared helpers `check.c`/`check.h` and `trace.c`/`trace.h` are still used by the remaining tests (`trace_replay`, `trace_replay_to` in `test_traces.c`). `test_wire.c` used only `check_*` and its own `check_same_config`. `test_format.c` tests `loop/ctl_format.h`, not the protocol, so it rightly stays. A grep of `HeishaMon/src/control`, `tests/control` and `docs/control` finds no other use of the protocol.
   5. **Work item hygiene.**
      - The journal names what was deleted, what stays and why (the faulty traces under `data/bad/`, the older decision-log entries), and that nothing outside `wire/` served only the protocol.
      - The new paragraph in `docs/control/core.md` claims `-ffp-contract=off` only for the host build, and the `Makefile` `CFLAGS` set it there.
      - `golden-traces.md` never mentioned the device replay, and no other document in `docs/control/` links to the removed parts.
      - The new 2026-11-14 entry in `decisions.md` gives the decision, the reason and the rejected option the story asks for.
   6. **Earlier tests.** Each deletion is covered by the story's Interface:
      - `tests/control/test_wire.c` (D): named by the story's Interface as deletable.
      - `tests/control/test_frame.c` (D): named by the story's Interface as deletable.
      - `tests/control/device/test_replay.py`, `garbling_twin.py`, `host_server.c`, `replay.py`, `replay/replay.ino` (D): everything under `tests/control/device/` is named by the story's Description as removed.
