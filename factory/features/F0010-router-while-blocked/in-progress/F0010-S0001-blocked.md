---
stage: doing
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: null
comments_read: []
handed_over: null
claim:
  instance: heishamon
  stage: doing
  started_at: '2026-11-12T22:52:33.252992Z'
  lease_duration: 60
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-12T22:52:33.246736Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-12T22:52:33.252992Z'
  stage: doing
---

# Assumed landing and the reason while blocked

## Description

The glue sends the router's commands only when commands may go to the pump: `enabled` is 1, HeishaMon is not in listen-only mode, and its optional PCB emulation is on. Its confirmation is the emulated frame, and it counts only while that frame goes out. On a blocked board, for example the owner's spare D1 that shadows the house with commands disabled, the confirmation is therefore never known. The router repeats its first send and ends in `FAULT`, which is not a real fault.

This story makes the glue step the loop with `assume_landed` true while commands are blocked, and with the confirmation from the frame as before while they are allowed. It also adds the telemetry entry `outputs/blocked`, which names why commands are blocked.

## Interface

Changed files: `HeishaMon/src/control/glue/ctl_telemetry.h`, `ctl_telemetry.c`, `ctl_firmware.cpp`, and in pure C a new `HeishaMon/src/control/glue/ctl_gate.h`, `ctl_gate.c` with the test `tests/control/test_gate.c`. `tests/control/test_telemetry.c` changes by the owner's leave as criterion 3 says.

```c
/* ctl_gate.h */
typedef enum { CTL_GATE_OPEN, CTL_GATE_DISABLED, CTL_GATE_LISTEN_ONLY, CTL_GATE_NO_OPTIONAL_PCB } ctl_gate;

ctl_gate ctl_gate_state(bool enabled, bool listen_only, bool optional_pcb);
const char *ctl_gate_name(ctl_gate gate);
```

- `ctl_gate_state` gives the first that holds, in this order:
  - `DISABLED` when `enabled` is false;
  - `LISTEN_ONLY` when `listen_only` is true;
  - `NO_OPTIONAL_PCB` when `optional_pcb` is false;
  - otherwise `OPEN`.
- `ctl_gate_name` gives `-`, `disabled`, `listen_only` and `no_optional_pcb`, and `invalid` for a value that is no member.
- **Telemetry.** `ctl_telemetry_source` gains `ctl_gate gate`. The outputs group gains `outputs/blocked` as its last entry, with `ctl_gate_name` of the source's gate, so the outputs count becomes 4.
- **The glue:**
  - it computes the gate with `ctl_gate_state` wherever it decided before whether commands may go out, and sends commands only when the gate is `OPEN`, as before;
  - before each tick it sets the loop configuration's `assume_landed` to whether the gate is not `OPEN`;
  - it passes the gate to the telemetry;
  - the reopening of the gate keeps the router reset with pacing as it is.

## Acceptance criteria

1. `ctl_gate_state`, as rows of one table test covering all eight combinations of the three flags, with the order above deciding.
2. `ctl_gate_name` for each member and for `(ctl_gate)9`.
3. The telemetry:
   - the outputs count is 4;
   - `outputs/blocked` gives `disabled` for a disabled gate and `-` for an open one.
   
   The earlier rows of `test_telemetry.c` that state the outputs count change by the owner's leave to 4, and no other row changes.
4. The loop with `assume_landed` true never reaches `FAULT`: a test runs `ctl_loop_tick` with no confirmation for 20 decision ticks with `assume_landed` true, and the router's status is never `FAULT`. The same run with it false reaches `FAULT`, as before.
5. `make firmware` compiles both boards.
6. `make check` stays green.
7. docs: `docs/control/firmware.md`, in *What reaches the pump*, names the gate and its reasons, the assumed landing while blocked and why, and the entry `outputs/blocked`. Its telemetry table lists the new entry.

## Smoke test

```sh
make firmware | grep -c -E '^(esp8266|esp32):'
```

Expect: `2`

## Journal

1. 2026-11-12 human: created.
2. factory: started doing as heishamon.
