# The device replay that survives a real serial line

## Goal

The first run of the device replay on the owner's D1 mini proved the core: all thirteen golden traces, 332,239 calls, 0 mismatches. But it needed a diagnostic copy of the sketch and a wrapper around the script, because the tooling as merged fails on that board. The board does not reset when its port opens, so the script never sees `ready`. At 921,600 baud the line garbles replies after about 26,000 calls. And printing a garbled reply crashes the script on the Windows console. This feature makes the merged tooling run on a real serial line as it is: every line carries a checksum, a garbled line restarts its trace from the beginning, the script resets the board itself, and the line runs at 230,400 baud. It is for the owner, who wants `make device-replay PORT=<port>` to work without help. Done when the story is merged, `make check` is green, and the replay of all thirteen traces on the D1 passes with the fork's files as they are.

## Scope

- A frame around every line: a checksum both directions, in C under `HeishaMon/src/control/wire/`.
- The sketch and the host twin check and add the frame; the host twin can garble a reply on purpose, for the test.
- The script frames its requests, checks every reply, restarts a trace after a broken line, resets the board, and prints safely.
- The documents of the device test.

## Out of scope

- Any change to the core or to the meaning of the requests in `ctl_wire.c`.
- Retrying a single call: a call changes the core's state, so sending it twice would apply it twice.

## Stories

- S0001: A checksum on every line, a restart after a broken one, a board reset and 230,400 baud

## The human's changes

None. The story changes only files under the builder's write paths.

## Decisions taken for the human

1. A broken line restarts its trace from the beginning, at most twice per trace, instead of retrying the call: the calls are not idempotent, and a replay from a cold start is exact by construction.
2. The checksum is the XOR of the line's bytes, two hexadecimal digits after ` *`. It is no cryptography; it only has to catch a dropped, repeated or flipped character, which is what the line did.
3. The line runs at 230,400 baud with 4 requests in flight: the speed at which the diagnostic run passed all thirteen traces in 15 minutes.
4. The earlier story's test files and its smoke test lines change by the human's leave, because the reply of the host twin now carries the frame.
