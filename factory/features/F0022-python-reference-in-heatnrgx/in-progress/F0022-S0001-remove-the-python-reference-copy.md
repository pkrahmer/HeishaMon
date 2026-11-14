---
stage: approval
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 38
comments_read: []
handed_over: null
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-14T19:10:43.758138Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-14T19:10:43.764065Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-14T19:15:11.004515Z'
  stage: doing
  run: cf9217c2ff034f62a812f70feb0ceefa
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-14T19:15:16.792378Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-14T19:15:56.235536Z'
  stage: review
  run: a4078be479764a759036826a03893d54
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-14T19:16:01.948214Z'
  stage: doing
- number: 7
  speaker: builder
  kind: result
  at: '2026-11-14T19:20:02.111754Z'
  stage: doing
  run: 395edf67ca524d32bb1044597a0e072a
- number: 8
  speaker: factory
  kind: started
  at: '2026-11-14T19:20:09.523081Z'
  stage: review
- number: 9
  speaker: reviewer
  kind: result
  at: '2026-11-14T19:20:34.555078Z'
  stage: review
  run: 1a32267c84d84a4cae521374aa63dbc3
---

# Remove the Python reference copy

## Description

**Why.** The fork carries a copy of the Python reference and its API manual, while the original lives in the owner's private repository heatnrgx, which also exports the golden traces. The copies already differ (heatnrgx has the integral term, the fork's copy does not). The golden traces, replayed bit for bit by `tests/control/test_traces.c`, are what guards the C core. The owner decided to keep the Python reference only in heatnrgx.

**What goes.** The folder `docs/control/python-reference/` with every file in it, and `docs/control/reference.md`.

**What changes.** Every place in the fork's documents (`docs/control/`), the control code's comments (`HeishaMon/src/control/`) and the tests (`tests/control/`) that names one of the removed files by its path is reworded: it points to the golden traces, to `core.md` or `golden-traces.md`, or names "the Python reference in heatnrgx" without a link. The phrase "the Python reference" may stay where it explains why the C code computes as it does. `docs/control/core.md` says in its opening where the reference lives now. The entries of `docs/control/decisions.md` stay as they were written.

**What stays.** The golden traces, every test, the core's code and its behaviour. No line of C code changes except comments.

## Interface

- Removed: `docs/control/python-reference/`, `docs/control/reference.md`.
- Comments only in `HeishaMon/src/control/` and `tests/control/`; no test's checks change.
- No upstream file changes, no hook line changes, no change to the `Makefile` or `CLAUDE.md`.

## Acceptance criteria

1. `git ls-files docs/control/python-reference docs/control/reference.md` prints nothing.
2. `git grep -n -E "python-reference|reference\.md" -- HeishaMon tests docs/control ':!docs/control/decisions.md'` prints nothing.
3. Every golden trace replays bit for bit in `tests/control/test_traces.c`, as before.
4. `git diff develop -- HeishaMon/src/control tests/control | grep -E '^[-+][^-+]' | grep -v -E '^[-+][[:space:]]*(/?\*|//|\*)'` prints nothing: only comment lines change in code and tests.
5. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
6. `make check` stays green.
7. docs: `docs/control/core.md` says in its opening that the Python reference lives in the owner's separate repository heatnrgx and that the golden traces exported from it are the reference the core is held to; `docs/control/golden-traces.md` says the same where it names their source; `docs/control/decisions.md` gets one entry: the copy of the Python reference removed from the fork, because two copies drift and the traces are the guard; rejected: keeping the copy in sync by hand.

## Smoke test

```sh
make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l
```

Expect: `1`

## Journal

1. 2026-11-14 human: created.
2. factory: started doing as heishamon.
3. builder: Built:
   - Removed `docs/control/python-reference/` (all 7 .py files) and `docs/control/reference.md` with `git rm`, so the deletions are already staged.
   - `docs/control/core.md`, opening: this was the only place outside `decisions.md` that named a removed path. It now says the Python reference lives only in the owner's separate repository heatnrgx, that the fork keeps no copy, and that the golden traces exported from heatnrgx (`golden-traces.md`) are the reference the core is held to and replays bit for bit.
   - `docs/control/golden-traces.md`: a new **Source.** paragraph under the Status line says the same and points to `tests/control/test_traces.c`.
   - `docs/control/decisions.md`: one new entry at the end, "The copy of the Python reference is removed". Reason: two copies drift, and the traces are the guard. Rejected: keeping the copy in sync by hand. The earlier entries are unchanged.
   - No C, header or test file changed. Code comments that say "the Python reference" without a path explain why the C computes as it does (ctl_balance.h, ctl_config.c, ctl_types.h, check.h, trace.h), so they stay as they are.

   Tests: I wrote no new test. Every criterion is a git or grep command, and the trace replay for criterion 3 is the existing `tests/control/test_traces.c`, which stays as it is.

   Checks:
   - Criterion 1 (`git ls-files` on the removed paths) prints nothing.
   - Criterion 2 (`git grep` for the removed paths) prints nothing.
   - Criterion 4 (only comment lines change) prints nothing, because nothing in code or tests changed.
   - Criterion 5 (`--stat` for upstream files) is empty.
   - `make check` is green, ending with `firmware: green (85.2 s)` and `check: green`.
   - Smoke test: the `make test | grep ... | wc -l` command printed `1`, which matches its Expect line.

   Noticed, not touched: `docs/control/core.md` (the paragraph on the integral term `p_integral_w`) says "It is not in the Python reference." That was true of the removed copy, but heatnrgx now has the integral term, so the sentence is out of date. The story only asked to reword lines that name a removed path, so I left it; the human may want it reworded in a later story. `golden-traces.md` also names heatnrgx's own `tests/test_golden.py`; that is a path in heatnrgx, not in the fork, so it stays.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Reviewed `git diff develop...HEAD -- . ':!factory'` against the work item. I ran criteria 1, 2, 4 and 5 again myself, and each printed nothing. `git diff --name-status` shows only M/D under `docs/control/`. No C, header or test file is touched. The journal says the factory's `make check` was green.

   Findings:

   1. `docs/control/core.md:147`: the sentence "It is not in the Python reference." is now false. This diff's new opening (`core.md:6-8`) defines "the Python reference" as the one that lives only in heatnrgx. The decision entry this diff adds (`decisions.md`, "The copy of the Python reference is removed") says heatnrgx has the integral term. So the fork's documents now contradict each other, and a reader of core.md would wrongly think the integral is a fork-only extension. The traces themselves carry no integral term: `zcat tests/control/data/*.trace.gz | grep -ci integral` finds 0 in every one. Fix: reword the sentence so it states what the fork can check, e.g. "The golden traces carry no integral term." The rest of the paragraph (operand order with the term off, bit-for-bit replay) can stay as written.

   Checklist groups with nothing to report:
   - Group 1, acceptance criteria: every criterion is a git or grep command, or the existing `tests/control/test_traces.c` (criterion 3), which is unchanged. The Interface's rules ("comments only", "no upstream change") are guarded by criteria 4 and 5.
   - Group 2, architecture: no file outside the three fork folders changed, and no hook line changed.
   - Group 3, embedded safety: no code changed.
   - Group 4, quality: nothing to report.
   - Group 5, docs: the `golden-traces.md` "Source." paragraph and the `decisions.md` entry match the docs criterion. The earlier entries in `decisions.md` are untouched. The `[A-n]` references in `golden-traces.md` were already there on `develop` and point to `algorithm-plan.md`, not to a removed file.
   - Group 6, earlier tests: the diff changes, renames or deletes no test file.
6. factory: started doing as heishamon.
7. builder: Round 1 fixes the reviewer's single finding.

   - `docs/control/core.md` (paragraph on the integral term `p_integral_w`): I replaced "It is not in the Python reference." with "The golden traces carry no integral term." This is the reviewer's suggested wording. It is a fact the fork can check, and it no longer contradicts the new opening or the decision entry that says heatnrgx has the integral term. The rest of the paragraph is unchanged, including the operand order with the term off and the bit-for-bit replay.

   Nothing else changed. No code, test or other document was touched in this round.

   Checks:
   - `make check` is green, ending with `firmware: green (85.9 s)` and `check: green`.
   - Criterion 2 (`git grep` for the removed paths) still prints nothing.
   - The smoke test printed `1`, which matches its Expect line.

   Noticed, not touched: nothing new.

   `check`: check: green
8. factory: started review as heishamon.
9. reviewer: no findings

   Round 1 review of `git diff develop...HEAD -- . ':!factory'` against the work item. I re-read the code myself and did not rely on the builder's account.

   - The round-0 finding is fixed. `docs/control/core.md:147` now says "The golden traces carry no integral term." That matches the traces, the new opening at `core.md:6-9` and the new `decisions.md` entry. The rest of the paragraph is unchanged.
   - `git diff --name-status develop...HEAD` lists only M and D entries, all under `docs/control/`. Seven files are deleted under `python-reference/`, plus `reference.md`. `core.md`, `decisions.md` and `golden-traces.md` are modified.
   - I re-ran criteria 1, 2, 4 and 5, and each printed nothing. The working tree is clean.
   - I also searched `docs/control`, `HeishaMon/src/control` and `tests/control` (without `decisions.md`) for "API reference", "reference manual", "beside this" and `.py`. This was to catch any reference to a removed file by name rather than by path. Nothing in the fork points to them. What is left is all in heatnrgx (`heatnrgx/sim/golden.py`, `tests/test_golden.py` in `golden-traces.md`), and the story allows that.

   Checklist:
   - Group 1 (acceptance criteria): criteria 1, 2, 4 and 5 are git and grep commands, and each printed nothing. Criterion 3 is the existing `tests/control/test_traces.c`, which is unchanged. Criterion 6: the journal says the factory's `make check` was green after the builder (entry 7). Criterion 7: the docs say what it asks for.
   - Group 2 (architecture): no file outside `docs/control/` changed. No upstream file, hook line, `Makefile` or `CLAUDE.md` changed.
   - Group 3 (embedded safety): no code changed.
   - Group 4 (quality): nothing to report. The remaining mentions of "the Python reference" (`core.md:91`, `:318`, `:369`) explain why the C computes as it does, and the story allows that.
   - Group 5 (work item hygiene): the journal gives the main decision and what was rejected. The Description matches what was built. The opening of `core.md`, the **Source.** paragraph in `golden-traces.md` and the new `decisions.md` entry (decision, reason, rejected: keeping the copy in sync by hand) match criterion 7. The earlier entries in `decisions.md` are untouched.
   - Group 6 (earlier tests): the diff changes, renames or deletes no test file.
