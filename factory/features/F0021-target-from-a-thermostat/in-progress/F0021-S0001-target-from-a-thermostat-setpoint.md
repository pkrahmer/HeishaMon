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
  started_at: '2026-11-14T12:08:25.193089Z'
  lease_duration: 60
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-14T12:08:25.186523Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-14T12:08:25.193089Z'
  stage: doing
---

# The target from a thermostat's setpoint

## Description

**Why.** The owner wants the house's target temperature to be set on a room thermostat, not only on the board's control page. The CCU publishes each thermostat's setpoint retained and on change, as JSON like `{"ts":1794646372801,"v":21,"s":0}`, the same form the room temperatures already use.

**The setting.** `desired_topic`: an MQTT topic, empty by default, with the same rules as the room topics. It is listed right after `desired_indoor_c`, stored in `/control.json` and shown on the page like the other settings. Setting or changing it subscribes to the new topic and forgets the setpoint of the old one; setting it empty unsubscribes.

**The rule.** The board holds the last valid setpoint from `desired_topic`. A message is valid when its payload parses as the room temperatures' payloads do and its value is finite and between 16 and 26 °C, both included. A valid message replaces the held setpoint; an invalid one (outside the range, unparsable, empty) clears it. While a setpoint is held, it is the controller's target (`desired_indoor_c` of the core's configuration); otherwise the setting `desired_indoor_c` is. A held setpoint never goes stale. Nothing of this is written to `/control.json`, and the setting `desired_indoor_c` keeps its own value.

**Applying it.** A change of the target takes effect for the next interval, the same way a change of the setting `desired_indoor_c` does today. The integral term is not touched.

**Telemetry.** `<base>/control/status/desired_c`: the target in use; `<base>/control/status/desired_source`: `setting` or `topic`. Both published with the other status values.

## Interface

- The glue: the setting, the subscription, the held setpoint, the rule and the telemetry. The rule (payload text and setting in, target and source out) is a pure function in a `.c` file, so that a host test checks it.
- The core does not change.
- Tests: a new host test `tests/control/test_desired_topic.c`. The leave for tests another story wrote: `tests/control/test_settings*.c`, `tests/control/test_telemetry*.c`, `tests/control/test_form*.c`, `tests/control/test_lean_page.c`, `tests/control/test_html.c` and `tests/control/test_dispatch.c` may gain rows for the new setting and status values, and their indexes and counts may shift; no check may be removed or loosened.
- No upstream file changes, and no hook line changes.

## Acceptance criteria

1. The rule, rows of one test, with `desired_indoor_c` 22: no setpoint held gives 22 and `setting`; payload `{"ts":1,"v":21,"s":0}` gives 21 and `topic`; `v` 16 and 26 are taken; `v` 15.9, 26.1, 5.5 and 4.5 clear the setpoint and give 22 and `setting`; an unparsable payload and an empty one clear it likewise; a valid setpoint after an invalid one is taken again.
2. With `desired_topic` empty, a message on the old topic changes nothing, and the target is `desired_indoor_c`.
3. Changing `desired_topic` forgets the held setpoint: the target is `desired_indoor_c` until a valid message arrives on the new topic.
4. The settings accept a topic and the empty value and refuse what the room topics refuse; a `/control.json` without `desired_topic` loads with it empty; the setting is listed right after `desired_indoor_c`.
5. A host test drives the loop with a held setpoint of 21 and `desired_indoor_c` 22, and checks that the core's balance uses 21; the core's files are unchanged (`git diff develop --stat -- HeishaMon/src/control/core` prints nothing).
6. The telemetry publishes `status/desired_c` and `status/desired_source`.
7. `make firmware` builds both boards; report the D1 mini's global RAM and flash before and after.
8. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
9. `make check` stays green.
10. docs: `docs/control/settings.md` gets `desired_topic` with the range, the fallback and an example (a Homematic `SET_POINT_TEMPERATURE` topic); `docs/control/firmware.md` describes the rule and the telemetry; `docs/control/decisions.md` gets one entry: the target from a thermostat's setpoint, 16 to 26 °C, never stale, not stored; rejected: the heat pump's curve shift (it also shifts the water temperature).

## Smoke test

```sh
make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l
```

Expect: `1`

## Journal

1. 2026-11-14 human: created.
2. factory: started doing as heishamon.
