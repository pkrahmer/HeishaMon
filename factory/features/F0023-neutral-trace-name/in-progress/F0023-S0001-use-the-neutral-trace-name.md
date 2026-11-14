---
stage: approval
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: 39
comments_read: []
handed_over: approval
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-14T19:21:59.626039Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-14T19:21:59.631917Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-14T19:28:42.721637Z'
  stage: doing
  run: df9e75b7fd03450bb4f1758af1a50fe8
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-14T19:28:51.321516Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-14T19:29:19.145892Z'
  stage: review
  run: d2de0602d365451586c2a324297cc896
---

# Use the neutral trace name

## Description

**Why.** The golden trace `barbarastr_march` is named after the owner's street, and the fork is public. The human added the same trace under the neutral name `house_march` (`tests/control/data/house_march.trace.gz`); apart from the name in its header lines, its data is identical line by line.

**What changes.** The trace test (`tests/control/test_traces.c`) and the integral test (`tests/control/test_integral.c`) read `house_march` instead of `barbarastr_march`, with the same call count, 129,493. `tests/control/data/barbarastr_march.trace.gz` is removed. `docs/control/core.md` and `docs/control/golden-traces.md` name the trace `house_march`; their description of it stays as it is.

**What stays.** Every other trace, every check, the core, and the entries of `docs/control/decisions.md`.

## Interface

- By the human's word, two tests another story wrote change: in `tests/control/test_traces.c` and `tests/control/test_integral.c`, only the name `barbarastr_march` becomes `house_march`. Nothing else in them changes.
- Removed: `tests/control/data/barbarastr_march.trace.gz`.
- No upstream file changes, and no change to the `Makefile` or `CLAUDE.md`.

## Acceptance criteria

1. `git grep -n -i barbarastr -- . ':!factory' ':!docs/control/decisions.md'` prints nothing, and `git ls-files tests/control/data | grep -c -i barbara` prints `0`.
2. `make test` prints the trace test's summary line with the same number of files, calls and 0 mismatches as on `develop`.
3. `git diff develop -- tests/control/test_traces.c tests/control/test_integral.c | grep -E '^[-+][^-+]' | grep -v -E 'barbarastr_march|house_march'` prints nothing.
4. `git diff develop --stat -- HeishaMon` prints nothing.
5. `make check` stays green.
6. docs: `docs/control/core.md` and `docs/control/golden-traces.md` use the new name; `docs/control/decisions.md` gets one entry: the March trace renamed to a neutral name, because the fork is public and the old name was the owner's street; rejected: rewriting the history.

## Smoke test

```sh
make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l
```

Expect: `1`

## Journal

1. 2026-11-14 human: created.
2. factory: started doing as heishamon.
3. builder: Built: `tests/control/test_traces.c` and `tests/control/test_integral.c` read `house_march` instead of `barbarastr_march`. The call count is still 129,493, and no other line in either file changed. `tests/control/data/barbarastr_march.trace.gz` is removed, with the deletion staged so that `git ls-files` no longer lists it. `docs/control/core.md` and `docs/control/golden-traces.md` name the trace `house_march` in their tables, with the descriptions unchanged. `docs/control/decisions.md` has a new entry: the March trace renamed to a neutral name because the fork is public and the old name was the owner's street; rejected: rewriting the history. The entry does not repeat the street name.

   Checked before removing: zcat-diff of the two traces differs only in header lines 3–4 (the scenario name).

   Decisions: I wrote no new failing-first test. This is a rename, and the existing trace and integral tests are the check: the trace summary is `traces: 13 files, 332239 calls, 0 mismatches` both with my changes and with them stashed (the develop state).

   Results: criteria 1, 3 and 4 commands print nothing, and the barbara count is 0. `make check` is green (lint, test, firmware). Smoke test: `make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l` gives `1` (expected `1`).

   Noticed, not touched: in test_integral.c's name list, `house_march` is now first, out of alphabetical order. I left it because the story allows only the name to change. The old name stays in the git history, as the decision records.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: no findings

   What I checked against the diff `develop...HEAD` (factory paths excluded) and the work item:

   1. Acceptance criteria. Criterion 1: the `git grep -n -i barbarastr` command (excluding factory and decisions.md) prints nothing, and `git ls-files tests/control/data | grep -c -i barbara` prints `0`. `docs/control/decisions.md` does not name the old name either. Criterion 2: the builder's journal gives the same summary on develop and on the branch (`13 files, 332239 calls, 0 mismatches`). The count stays the same because `test_traces.c` keeps `{"house_march", 129493}`. I also compared the unpacked develop `barbarastr_march` with `house_march`: they differ only in lines 3–4, the header's scenario name. Criterion 3: in each test file, the one changed line only swaps the name. Criterion 4: `git diff develop --stat -- HeishaMon` prints nothing. Criterion 5: the factory's check after the builder is `check: green`. Criterion 6: core.md and golden-traces.md swap only the name and keep the description. decisions.md has one new entry with the date, decision, reason, and the rejected option (rewriting history), as the criterion asks. The criteria are checked by commands, and the existing trace and integral tests cover the behaviour. No new test is needed for a rename.
   2. Architecture: no file outside `docs/control/` and `tests/control/` changed, and no code under `HeishaMon/` changed.
   3. Embedded safety: no code that runs on the board changed.
   4. Quality: the change is limited to the names. `house_march` now heads the name list in `test_integral.c` out of alphabetical order. The story allows only the name to change, so this is correct.
   5. Work item hygiene: the journal explains the rename, the zcat comparison before the removal, and why there is no new test. The Description matches what was built, and the documents' names match the code.
   6. Earlier tests changed:
   - `tests/control/test_traces.c` (M): the human approved the change in the story's Interface. Only the name changed.
   - `tests/control/test_integral.c` (M): the human approved the change in the story's Interface. Only the name changed.
   - `tests/control/data/barbarastr_march.trace.gz` (D): test data. The Description and Interface name its removal, and `house_march.trace.gz` holds the same data.
