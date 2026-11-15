# Give RAM back to the web server

## Goal

The owner's production board (ESP8266) restarts when the HeishaMon main page is loaded. F0024's reset reason and a bench reproduction (a spare D1 with an emulated pump) showed why: while upstream's web server builds the main page and its 13.7 KB `/json` answer, the heap runs out; allocations fail, upstream restarts on purpose or the Wi-Fi driver writes through a null pointer (exception 29). Free heap is about 22 KB, the largest block 17 KB, fragmentation 24 % a few minutes after boot. The fork's control code holds 8.4 KB of static RAM: 4.3 KB of variables and 4.0 KB of constant text that the ESP8266 keeps in RAM unless it is placed in flash. This feature gives as much of it back as the control code can spare without changing what it does. Done when both stories are merged, `make check` is green, and the D1 mini's global RAM is at least 3,500 bytes below `develop` at the feature's start.

## Scope

- Constant text of the control code in flash on the ESP8266 (S0001).
- Fewer room slots, and shared scratch buffers in the glue (S0002).

## Out of scope

- Upstream files and upstream's web server.
- Any change of behaviour, topics, settings or the page's content.

## Stories

- S0001: Constant text in flash
- S0002: Fewer room slots and shared buffers

## The human's changes

None. The write paths were checked: both stories stay in `HeishaMon/src/control/`, `tests/control/` and `docs/control/`.

## Decisions taken for the human

1. The room limit goes from 16 to 8: the owner uses 5, and each slot costs about 100 bytes in several tables. A `/control.json` with more than 8 rooms loads the first 8 and refuses the rest, as an out-of-range setting is refused today.
2. The baseline for "below `develop`" is the D1 mini's global RAM of the commit that starts S0001, 49,108 bytes on 2026-11-16.
