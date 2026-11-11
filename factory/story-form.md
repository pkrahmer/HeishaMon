# Story title

A story is one file, `factory/features/<F0001-slug>/backlog/<F0001-S0001-slug>.md`, without front matter. Its first line is its title, `# <what the story builds>`. It has the five sections below, in this order; the factory checks it against this form before a stage moves it forward (`factory-check-story <path>`).

## Description

What the story builds and why, for whom, in a paragraph: what changes, what stays as it is, and what the story leaves to a later one. Every behaviour named here has its own criterion.

## Interface

The modules, classes and functions the story adds or changes, with their signatures in a code block and their rules as bullets: each rule a sentence a test can check, with every limit on both sides. The test files the tester writes, and a test another story wrote that this story changes by the human's leave, named here.

## Acceptance criteria

Numbered 1, 2, 3 and on; each one behaviour a test can check, with its input and its exact result. The sides of one rule (accepted and refused, below and at and above a limit) are one criterion, tested as rows of one parametrized test. The second-to-last criterion is `make check` stays green. The last starts with `docs:` and says what the documenter writes, in which file.

## Smoke test

Shell blocks the smoke tester runs from the root of the checkout, each followed by a line `Expect:` that says exactly what the built code prints. A block that needs a file or a server works in a temporary directory it removes (`d=$(mktemp -d); trap 'rm -rf "$d"' EXIT`) and stops what it started.

## Journal

Empty in the backlog. The factory writes entry 1 when the story starts.
