# The control page in HeishaMon's web interface

## Goal

The loop inside the firmware publishes everything over MQTT and takes its settings over MQTT. This feature gives it a page in HeishaMon's own web interface, `/control`, so that the owner can see the loop's status and inputs and change its settings in the browser, without an MQTT tool. The page shows the same entries as the telemetry, and saves through the same setter as the MQTT setting topics: one validation, one file. It is for the owner, who commissions the loop at the heat pump. Done when the story is merged, `make check` is green, and the page loads on a board.

## Scope

- The page `/control`: status, outputs and inputs as tables, and a form with every setting.
- The form's target `/savecontrol`: applies every field through `ctl_settings_set`, saves the file when anything changed, and returns to `/control` with what was accepted and refused.
- Two hook lines in upstream's files: one at the start of `webserver_cb` in `HeishaMon.ino` that hands the two routes to the glue, and one menu link in `htmlcode.h`.

## Out of scope

- Live updates on the page: it is a static page with a reload link.
- Upstream's own settings page and its settings.

## Stories

- S0001: The control page

## The human's changes

The session in the owner's seat adds `HeishaMon/htmlcode.h` to the builder's write paths, and the leave for exactly one hook line there to `CLAUDE.md` and the two agent files, before S0001.

## Decisions taken for the human

1. One hook line at the very start of `webserver_cb` hands every step of the two routes to the glue, instead of a case in each of the callback's six switches: one line in upstream instead of a dozen.
2. The page uses upstream's frame (header, CSS, body start, menu, footer), so that it looks like the rest of HeishaMon.
3. The form shows the fields of the current `rooms` count; to add rooms the owner first saves a new `rooms` count, then fills the new fields.
