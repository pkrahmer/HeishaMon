# Why the board restarted

## Goal

The owner's production board (HeishaMon small board, ESP8266) restarts when the HeishaMon main page is loaded on it, and on 2026-11-15 it restarted three times within half an hour after one visit. It had run 32 hours without a problem before. Its free heap stays around 22 KB, so the suspicion is a fragmented heap or a crash in the web server, but the board has no serial cable on the production pump, so the crash message is lost. This feature makes the board tell why it restarted, and how fragmented its heap is, over MQTT. With the release ELF the owner keeps, a crash address can then be decoded to a function. Done when the story is merged and `make check` is green.

## Scope

- After every boot, publish the reset reason and, for an exception, its code and addresses, once, retained.
- Publish the largest free heap block and the heap fragmentation with the regular status values.

## Out of scope

- Any fix of the crash: that follows once the cause is known.
- Upstream files.

## Stories

- S0001: The reset reason and the heap's shape

## The human's changes

None. The write paths were checked: the story stays in `HeishaMon/src/control/`, `tests/control/` and `docs/control/`.

## Decisions taken for the human

1. The reset reason is published retained, so that it can still be read hours later, and once per boot.
2. On the ESP32-S3 the same topics carry what that board offers; a field it does not have is left out.
