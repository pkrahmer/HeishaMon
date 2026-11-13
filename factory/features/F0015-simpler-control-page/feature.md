# A simpler control page

## Goal

The owner finds the control page's settings hard to read. After a save, a second table "Last save" lists every setting again with "accepted" or "refused", far from the field it is about, and nothing on the page says what a setting is for. This feature puts the save result into one column next to each value, shows it only where something happened, and gives every setting a short description with its unit and allowed values. Done when the story is merged, `make check` is green, and the owner can read on the page what each setting does.

## Scope

- The settings table: one row per setting with its name, its value field, a short description, and the result of the last save for that setting.
- The separate "Last save" table goes away.

## Out of scope

- The status and inputs tables stay as they are.
- What a setting does, its name, its rules and its storage.

## Stories

- S0001: Descriptions and the save result per row

## The human's changes

None.

## Decisions taken for the human

1. The descriptions are written for the owner, in one short sentence each, with the unit and the allowed values.
2. After a save, a row shows "saved" when its value changed, "refused" with the reason when it was refused, and nothing when it was sent unchanged.
3. The descriptions live in flash on the ESP8266, not in RAM.
