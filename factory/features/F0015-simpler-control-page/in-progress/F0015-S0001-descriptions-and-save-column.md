---
stage: review
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 29
comments_read: []
handed_over: null
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-13T12:27:04.333735Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-13T12:27:04.339701Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-13T12:44:10.719489Z'
  stage: doing
  run: ce87a44d92f246eabfe176d9e9ce63d7
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-13T12:44:16.181269Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-13T12:46:49.739926Z'
  stage: review
  run: 2de69796d51d41d3a1deaa21d53549c3
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-13T12:46:55.434344Z'
  stage: doing
- number: 7
  speaker: builder
  kind: result
  at: '2026-11-13T12:53:31.373300Z'
  stage: doing
  run: dbe22c4bd548479486cadf06017c5a8a
---

# Descriptions and the save result per row

## Description

**Why.** On the control page (`/control`), the settings table shows a name and a value field per setting. After a save, the page adds a second table, "Last save", that lists every submitted name again with "accepted" or "refused". The owner finds this confusing: the result sits far from the field it is about, every unchanged setting shows "accepted" too, and nothing says what a setting is for.

**The new settings table.** One row per setting, in today's order, with four columns:

1. **Setting**: the name, as today.
2. **Value**: the field, as today.
3. **What it does**: the description below, in one line or two.
4. **Last save**: empty, except right after a save:
   - `saved` when the save changed this setting's value;
   - `refused: <reason>` when this setting's value was refused, where the reason is `not a valid value` (the setter answered `BAD_VALUE`), `conflicts with other settings` (`INVALID_SET`) or `unknown setting` (`UNKNOWN`), and the field shows the value that is still in force;
   - empty when the value was sent unchanged.

The table "Last save" and anything that only built it go away. A refused row is visibly marked (for example, its text in the page's warning colour); a saved row is not marked beyond the word.

**The descriptions.** Each in the owner's words, with the unit and the allowed values. For a room, the description names the room's number. Before writing them into the code, the builder checks each against the code and the docs; where the code says otherwise, the builder corrects the wording to match the code and lists each correction in its result.

| Setting | What it does |
|---|---|
| `enabled` | Send commands to the pump: 0 = only watch and decide, 1 = control the pump. |
| `cooling` | Allow cooling demand: 0 = heating only, 1 = heating and cooling. |
| `rooms` | Number of room sensors in the indoor average, 0 to 16. Save first; the room fields then appear. |
| `room.<i>.topic` | MQTT topic of room `<i>`'s temperature. |
| `room.<i>.weight` | Weight of room `<i>` in the indoor average, 0 to 1000. |
| `room_timeout_s` | Seconds after which a room's last value counts as too old. |
| `pv_topic` | MQTT topic of the PV power in W, for the solar gains. Empty: no PV. |
| `frame_log_topic` | Another HeishaMon's log topic to read the pump's frames from. Empty: this board's own pump. |
| `desired_indoor_c` | Target for the indoor average, in °C. |
| `transmission_w_per_k` | The house's heat loss per kelvin between inside and outside, in W/K. |
| `adjustment_w_per_k` | Extra power per kelvin the indoor average is off its target, in W/K. |
| `hysteresis_wh` | The energy balance, in Wh, at which the pump is switched on (below minus this) or off (above plus this). |
| `trim_limit_k` | The largest deviation from the target, in K, that the adjustment acts on. `none`: no limit. |
| `solar_gain_factor` | Share of the PV power counted as solar heat entering the house. |
| `internal_w` | Heat from people and devices inside, in W. |
| `heat_cool_heat_value` | The `SetHeatCoolMode` value that means heat; -1 never sends it. Used only without Smart Grid. |
| `min_change_interval_s` | Shortest time between two switches of the pump, in seconds. |
| `retry_interval_s` | Seconds before a command that was not confirmed is sent again. |
| `max_retries` | Repeats of an unconfirmed command before the router reports a fault. |
| `smart_grid` | How the pump is switched: 1 = Smart Grid mode (0 heating allowed, 1 = Capacity 1), 0 = the external thermostat input. |
| `command_topic` | Another HeishaMon's commands topic to send commands to. Empty: this board's own serial port. |

If F0014 has removed a setting from this list, its row is simply absent; a setting this list misses gets a description in the same style, listed in the result.

**Memory.** The descriptions must not take RAM on the ESP8266: they live in flash, as the page's other fixed texts do. The builder states in its result the free heap change it expects, and `make firmware`'s memory lines show the RAM use before and after.

## Interface

- The descriptions are reachable by setting name through one function in the fork's own code, so that a host test can check that every listed setting has one. Its form is the builder's choice; a room's description takes the room's number.
- The leaves for tests another story wrote: `tests/control/test_form.c` and `tests/control/test_html.c` may change where they test the "Last save" table or the settings table's columns. No other test changes. New tests go into new files.
- No upstream file changes, and no hook line changes.

## Acceptance criteria

1. A host test: every name that `ctl_settings_name_at` lists, with `rooms` 2, has a non-empty description, and the two rooms' descriptions name their numbers.
2. A host test of the settings table's rows after a save that changed `hysteresis_wh`, sent `enabled` unchanged and refused `rooms` `99`: the row of `hysteresis_wh` says `saved`, the row of `enabled` has an empty last column, and the row of `rooms` says `refused: not a valid value` and shows the value still in force.
3. The page has no table "Last save" any more: no text `Last save` appears in what the page writes.
4. `make firmware` compiles both boards, and its memory lines for the D1 mini show no more than 200 bytes more of global RAM than on `develop`.
5. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
6. `make check` stays green.
7. docs: `docs/control/firmware.md`, section *The control page*, describes the four columns and the save results, and no longer the "Last save" table.

## Smoke test

```sh
make firmware | grep -c -E '^(esp8266|esp32):'
```

Expect: `2`

## Journal

1. 2026-11-13 human: created.
2. factory: started doing as heishamon.
3. builder: Built the four-column settings table with descriptions and a save result in each row. The "Last save" table is gone.

   **What I built**
   - `HeishaMon/src/control/glue/ctl_settings.{h,c}`: new `ctl_settings_describe(settings, name, out, size)` and `CTL_SETTINGS_DESCRIPTION_MAX` (192). The descriptions are one table indexed by `setting_id`, with `#` where a room's number goes. A compile-time check stops the build when a setting has no slot. On the ESP8266 the table is `PROGMEM` and read with `pgm_read_byte` from the toolchain's newlib header `<sys/pgmspace.h>`, included only when `ESP8266` is defined. Everywhere else it is a plain constant, so lint and host tests run as strict C99.
   - `glue/ctl_form.{h,c}`: the report keeps one `uint8_t` `ctl_save_result` per applied name: saved, unchanged, or refused with the setter's reason (BAD_VALUE, INVALID_SET or UNKNOWN). A field too long for any setting counts as a bad value. New functions: `ctl_form_report_result` and `ctl_form_row` (builds one settings row). Removed: `saved`, `accepted[]`, `ctl_form_report_items` and `ctl_form_report_item`, plus their HTML strings.
   - `glue/ctl_html.{h,c}`: `ctl_html_input` is replaced by `ctl_html_setting_row` with a `ctl_html_setting` struct, and `CTL_HTML_TOO_LONG` is now shared. A refused row starts `<tr style='color:var(--orange)'>`.
   - `glue/ctl_web.cpp`: `SECTION_SAVE` is removed, and `setting_row` now calls `ctl_form_row`. `FORM_OPEN` gains a header row. The report is emptied once the page after a save has been written in full.
   - Tests:
     - New `tests/control/test_descriptions.c` covers criterion 1, plus the refusals and buffer sizes.
     - New `tests/control/test_form_rows.c` covers criterion 2 (full expected rows for `hysteresis_wh`, `enabled` and `rooms` 99), each refusal reason, an empty last column with no save, the row without an input, and criterion 3. For criterion 3 it scans the rows and the sources `ctl_web.cpp`, `ctl_form.c` and `ctl_html.c` for `Last save`.
     - `tests/control/test_form.c`: only the report parts changed. `check_report_entry` now checks results, and `test_report_text` and the `report.saved` checks are gone.
     - `tests/control/test_html.c`: `check_input` is replaced by `check_setting`.
   - Docs: `docs/control/firmware.md`, section *The control page*, now covers the four columns, the save results and the memory paragraph. Added an entry to `docs/control/decisions.md`.

   **Decisions** (recorded in decisions.md)
   - **Column heading:** *Save result*. Criterion 3 forbids the text "Last save" anywhere on the page, so the story's own column name could not be used.
   - **When results show:** only on the page that follows the save; a reload shows none ("right after a save").
   - **Room numbers:** counted from 0, as in `room.0.topic`.
   - **Refused-row colour:** an inline style with `var(--orange)`, because a CSS class would mean changing `htmlcode.h`.
   - **Rejected:**
     - Descriptions as `FLASHPROG` texts in the `.cpp`, which no host test can reach.
     - A second table of names for the descriptions.
     - Hand-written aligned word reads from flash.
     - Keeping results until the next save.

   **Wording corrections against the code**
   - `hysteresis_wh`: the pump does not switch off at plus this. Heating starts at minus H and cooling at plus H, and both stop when the balance is back at 0 (`ctl_relay`). It must also be above 0.
   - `smart_grid`: 0 is sent for heating *or cooling* demand, and 1 (Capacity 1) for no demand.
   - Allowed values added where the code enforces them:
     - `room_timeout_s`, `transmission_w_per_k`, `trim_limit_k`: above 0.
     - `adjustment_w_per_k`, `solar_gain_factor`, `min_change_interval_s`, `retry_interval_s`: 0 or more.
     - `desired_indoor_c`: 5 to 30.
     - `heat_cool_heat_value`: 0 or 1, and -1 never sends it.
     - `max_retries`: 0 to 10 (`ctl_router_config_valid`).
   - No setting is missing from the owner's list, and none has been removed.

   **Memory**
   - D1 mini global RAM: 50564 bytes on develop, 50628 now (+64, under the 200 limit). Flash grew by 4480 bytes; the description table is 21 × 192 bytes in flash.
   - Expected free heap change: −64 bytes, from the short result texts in `ctl_form.c` and `ctl_html.c`.
   - Rendering one row now puts about 320 more bytes on the stack, briefly.

   **Checks**
   - `make check` is green (lint, test, device and firmware).
   - Smoke test: `make firmware | grep -c -E '^(esp8266|esp32):'` printed `2`, as expected.
   - `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` is empty.

   **Noticed, not touched**
   - `FORM_CLOSE` still says to save the count of rooms first, which the `rooms` description now repeats.
   - The comment on `command_topic` in `ctl_settings.h` says "the base … as `<base>/commands`", while firmware.md and the publishing code use the commands topic itself.
   - `test_form_rows.c` has its own short copy of `test_form.c`'s setter: that one is `static`, and `test_form.c` may change only where it tests the report.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Reviewed `git diff develop...HEAD`. Checked these against the code, and all hold:
   - Every corrected description and range matches the code: `ctl_relay`, `ctl_config_validate` (5 to 30), `ctl_router_config_valid` (0 to 10, -1 to 1), the Smart Grid comment in `ctl_router.c`, `CTL_BOUND_WEIGHT` and `parse_whole(0, CTL_ROOMS_MAX)`.
   - Criteria 1–3 each have a test that checks them. Row and description sizes are tested on both sides, and names the function refuses are tested too.
   - `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` is empty.
   - `<sys/pgmspace.h>` is the toolchain's libc header (`xtensa-lx106-elf/include/sys`), not Arduino's or upstream's, and the `.c` file still lints as strict C99.
   - The page is served as UTF-8, so the `°` in a description shows correctly.
   - `firmware.md` *The control page* matches the code: four columns, results emptied once the page has been written, too-long rows.
   - The factory's `make check` after the builder was green.

   Earlier tests:
   - `tests/control/test_form.c` (M): only the report's checks changed, the data that built the "Last save" table. Allowed by the story's leave.
   - `tests/control/test_html.c` (M): `check_input` was replaced by `check_setting` for the settings table's columns. Allowed by the story's leave.

   Findings:
   1. Story `## Description`, item 4 of "The new settings table": it names the column **Last save**, but the page's heading is "Save result" (`ctl_web.cpp` `FORM_OPEN`), renamed because criterion 3 forbids the text. Fix: rewrite the Description to name the column "Save result" and give the reason (R5).
   2. `tests/control/test_form_rows.c` `set_setting`: the comment calls it "the real settings setter", but it sets `*changed = false` for a name that was not known before the set. `ctl_firmware_change` (`ctl_firmware.cpp:350`) counts that as changed (`!known || …`). Fix: use the firmware's rule, or say in the comment how it differs.
   3. `docs/control/decisions.md`, entry "The save result in the settings table's rows":
      - "The descriptions are about 1.8 kB" is wrong: the table takes 21 × 192 = 4032 bytes.
      - "a text that outgrows it stops the build" is false for a text of exactly 192 characters, which C accepts without a closing zero. Only `test_descriptions.c` catches it.

      Fix: correct both sentences.
6. factory: started doing as heishamon.
7. builder: Round 1: fixed the three review findings and nothing else.

   **What changed**
   1. **Story Description, item 4 of "The new settings table":** the column is now named **Save result**, with the reason: criterion 3 forbids the text "Last save" anywhere on the page. This matches the heading in `ctl_web.cpp` `FORM_OPEN`.
   2. **`tests/control/test_form_rows.c` `set_setting`:** it now uses the same rule as `ctl_firmware_change`. After a successful set, a value counts as changed when the name was not known before, its value cannot be read back, or its text differs. The comment now states that rule instead of only "the real settings setter". The test's own expected rows did not change.
   3. **`docs/control/decisions.md`, entry "The save result in the settings table's rows":**
      - The size is now given as 21 × 192 = 4032 bytes. "About 1.8 kB" is gone.
      - The slot sentence now says the build stops only for a text longer than its slot. C accepts a text of exactly the slot's length with no closing zero. In that case `ctl_settings_describe` refuses it for a buffer of the slot's size, so `test_descriptions.c` fails.

   **Rejected:** a compile-time check for a text of exactly 192 characters. The finding asked only for the sentences to be corrected, and the host test already catches that case.

   **Checks**
   - `make check`: green (`check: green`). D1 mini global RAM is still 50628 bytes.
   - Smoke test: `make firmware | grep -c -E '^(esp8266|esp32):'` printed `2`, as expected.

   **Noticed, not touched:** the same items as in round 0:
   - `FORM_CLOSE` repeats the hint about saving the room count first.
   - The comment on `command_topic` in `ctl_settings.h` describes the topic as a base, which is wrong.

   The factory left out changes outside the write paths: factory/features/F0015-simpler-control-page/in-progress/F0015-S0001-descriptions-and-save-column.md.

   `check`: check: green
