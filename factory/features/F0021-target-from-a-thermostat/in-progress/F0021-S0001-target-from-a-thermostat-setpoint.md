---
stage: review
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: 36
comments_read: []
handed_over: null
claim: null
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
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-14T12:21:48.620766Z'
  stage: doing
  run: b87020091b8e407f9fb79e79f67a6802
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
3. builder: Built the target from a thermostat's setpoint. Final `make check` is green: lint, test (34 control programs and upstream's 36 suites) and firmware for both boards.

   **What I built**
   - `HeishaMon/src/control/glue/ctl_desired.h` and `ctl_desired.c` (new, pure C99) hold the rule, in four functions:
     - `ctl_desired_offer` parses the payload with `ctl_payload_parse`, the same parser the rooms use. It holds a finite value from 16 to 26 °C, both included, and clears the setpoint on anything else (empty, NULL or unparsable included).
     - `ctl_desired_target` gives the target and its source: the held setpoint (`topic`), else the setting `desired_indoor_c` (`setting`).
     - `ctl_desired_apply` writes that target into the core's `desired_indoor_c` in the loop's configuration.
     - `ctl_desired_follow` forgets the held setpoint when `desired_topic` differs from the topic it came from.
   - `glue/ctl_settings.h` and `ctl_settings.c`: new setting `desired_topic`, empty by default, with the same parser as the room topics. It is listed right after `desired_indoor_c`, so `CTL_SETTINGS_FIXED_NAMES` goes from 17 to 18.
   - `glue/ctl_dispatch.h` and `ctl_dispatch.c`: a new kind `CTL_TOPIC_DESIRED` for a topic equal to `desired_topic`. Rooms are matched first, and an empty topic still matches nothing.
   - `glue/ctl_telemetry.h` and `ctl_telemetry.c`: the telemetry source gets `desired_c` and `desired_source`. Two new status entries, `status/desired_c` (2 decimals) and `status/desired_source`, bring the status count from 12 to 14.
   - `glue/ctl_firmware.cpp`:
     - A new `configure_loop()` runs `ctl_settings_to_loop`, then `follow`, then `apply`. It is used at setup and in `ctl_firmware_change_end`, so a new target takes effect from the next interval, as a change of the setting does. The integral term is not touched.
     - `desired_topic` is subscribed with the room topics, and `moves_topics` includes it, so changing it re-subscribes and emptying it unsubscribes.
     - `handle_desired` offers the payload, applies the result and logs when the target changes or a message is refused.
     - The target and its source are captured at each decision for the telemetry.
     - Nothing of this is saved to `/control.json`.
   - Tests:
     - New `tests/control/test_desired_topic.c` covers criteria 1–5:
       - the rule rows;
       - a valid setpoint after an invalid one;
       - an empty or changed topic;
       - the setting's accept and refuse rows and its position in the list;
       - a file without the setting loading with it empty;
       - a loop run that checks the decision's `p_trim_w` equals the balance computed at 21 and differs from the one at 22.
     - `test_settings.c` gains a defaults row, the name list entry and `desired_topic` in the topic rows. Counts shift from 17 to 18, 21 to 22 and 19 to 20, and indexes from 19–21 to 20–22.
     - `test_telemetry.c` gains the two status rows, a `setting`-source case and fixture fields. The status count shifts from 12 to 14, and the refused index from 12 to 14.
     - No check was removed or loosened.
   - Docs:
     - `docs/control/settings.md`: the table row and a section with the range, the fallback and a Homematic `SET_POINT_TEMPERATURE` example.
     - `docs/control/firmware.md`: a settings row, the topic note, a new section *The target from a thermostat*, the status table, and the re-subscribe note.
     - `docs/control/decisions.md`: one entry.

   **Decisions taken where the story left room** (all recorded in decisions.md)
   - No replay or timestamp check on the setpoint, unlike the rooms: a replayed setpoint is the same setpoint.
   - A room whose topic equals `desired_topic` keeps the messages, because rooms are matched first.
   - `status/desired_c` is reported as the target at the last decision, with 2 decimals like the room values.
   - An out-of-range value clears the setpoint rather than being clamped. Rejected: clamping, since a frost setting would wrongly become 16 °C. Also rejected: a timeout, and the heat pump's curve shift (as the story says).

   **Results**
   - Criterion 5: `git diff develop --stat -- HeishaMon/src/control/core` prints nothing.
   - Criterion 8: `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` prints nothing, so no upstream file and no hook line changed.
   - Criterion 7, D1 mini, `develop` (built from a `git archive` copy in a temp folder) versus this branch:
     - Global RAM: 48244 → 48596 bytes (+352).
     - Flash: 757144 → 758184 bytes (+1040).
     - The ESP32-S3 builds too: sketch 1705199 → 1706155 bytes, globals 67368 → 67608 bytes.
   - Smoke test: `make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l` printed `1`, as expected.

   **Noticed and changed in passing**
   - firmware.md's status table said "10 entries" and lacked `status/p_integral_w` and `status/integral_wh`, which the code already publishes. I corrected the count to 14 and added those two rows while documenting the new ones.

   **Noticed, not touched**
   - After a change of `desired_topic`, the broker still holds the old retained `config/desired_topic` value only until the glue answers with the new one. This is the same as for every setting topic, so nothing new.

   `check`: check: green
