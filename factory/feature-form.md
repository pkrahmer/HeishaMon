# Feature title

A feature is one directory, `factory/features/<F0018-slug>/`, with this file as `feature.md` and its stories in `backlog/`. The factory reads its title (the first line) for the acceptance; the acceptance reviewer reads the rest. It has the sections below, in this order. The human writes it and keeps it; no stage edits it.

## Goal

Why the feature exists, for whom, and what is different when it is done, in a paragraph. Done when every story below is merged and `make check` is green.

## Scope

- What belongs to it, one bullet each.

## Out of scope

- What does not, so that no story drifts there.

## Stories

The build order; start one at a time, the next when the one before is archived in `done/`.

- S0001: <title>

## The human's changes

What lies outside every write path (`Makefile`, `pyproject.toml`, `factory/`, `security/`, `CLAUDE.md`, `.secrets.baseline`) and a story needs, with the exact lines and the story it must come before; or `None`, with the write paths checked.

## Decisions taken for the human

Numbered; each decision with its reason and where it was taken (a story's text, an answer on a pull request), for the human to confirm in one place. A leave to change a test another story wrote is one of them, naming the test modules.
