# The fork moves to upstream v4.4.1

## Goal

The owner's production board restarted when the HeishaMon page was open during a full retransmit of all topics. The fork's bench found the trigger, the owner reported it upstream, and upstream fixed it in release v4.4.1: one log line for a full retransmit, and a bounded websocket queue on the ESP8266. On 2026-11-16 the bench confirmed the fix: upstream before it restarted twice in 12 minutes under the browser load, upstream with it not at all. This feature brings the fork onto v4.4.1, so that the owner can run the control loop on a firmware that no longer needs the long retransmit interval as a workaround. Done when every story below is merged and `make check` is green.

## Scope

- Upstream v4.4.1 merged into `develop` (the human's change below, done).
- The Control link in v4.4.1's navigation, and the control page inside v4.4.1's page frame (S0001).
- The fork's documents name v4.4.1 as their base and record the restart finding (S0001).

## Out of scope

- Any change of the control loop's behaviour, topics, settings or stored files.
- Any change to upstream's files beyond the one hook line S0001 names.
- The release image and its flash: the owner's, after the feature.

## Stories

- S0001: The control page in v4.4.1's navigation

## The human's changes

1. Before S0001, done on 2026-11-16 as pull request 46: upstream's tag `v4.4.1` merged into `develop`. One conflict, in `HeishaMon/htmlcode.h`: upstream moved the side navigation from a script block into the shared page header, so the fork's Control link in the old block was dropped. `make check` was green on the merged tree in the project image.

## Decisions taken for the human

1. S0001 may change the tests of the control page's top that earlier stories wrote, where they pin the page's own navigation script: that script goes.
2. The retransmit interval on the owner's board stays the owner's setting; no story changes a default.
