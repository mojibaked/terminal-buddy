# Compatibility Checklist

## Purpose

This document turns the compatibility plan into a repeatable manual test pass.

Use it to answer two questions:

1. Does dictation commit text in the right place?
2. Which insertion path was used and where did it fail?

This checklist is intentionally focused on Tier 1 targets from the main plan.

Related document:

- [Dictation Everywhere Plan](./dictation-everywhere-plan.md)

## Test Session Metadata

Record this at the top of each test pass:

- date
- build or git commit
- Windows version
- app integrity level: normal or elevated
- control mode used: widget or hotkey
- insertion mode used: auto, TSF preferred, or fallback only
- microphone device
- whether the TSF text service is installed and enabled

## Shared Test Phrases

Use the same phrases every time so failures are comparable.

- `Phrase A`: `alpha bravo charlie`
- `Phrase B`: `hello comma world period`
- `Phrase C`: `replace me now`
- `Phrase D`: `one two three four five`

Terminal-specific phrases:

- `Terminal A`: `echo alpha bravo charlie`
- `Terminal B`: `git status`
- `Terminal C`: `python main dot py`

## Shared Evaluation Rules

Mark each scenario with one result:

- `pass`
- `flaky`
- `fail`
- `not tested`

For every failure or flaky result, record:

- exact app and field
- whether dictation started from widget or hotkey
- whether TSF or fallback was used
- the visible wrong behavior
- whether focus changed unexpectedly
- whether clipboard contents changed

## Shared Scenarios

Run these scenarios in every Tier 1 app unless the app section says otherwise.

### Scenario 1: Final Commit at Caret

1. Focus an empty text field.
2. Start dictation.
3. Speak `Phrase A`.
4. Stop dictation if needed.
5. Confirm the exact text appears at the caret with no extra leading or trailing characters.

Expected result:

- final text appears once
- text lands in the focused field
- focus stays in the field

### Scenario 2: Punctuation Handling

1. Focus an empty text field.
2. Start dictation.
3. Speak `Phrase B`.
4. Confirm punctuation is committed in the expected textual form for the active mode.

Expected result:

- punctuation behavior matches product rules
- no duplicated words
- no accidental newline or submit

### Scenario 3: Replace Selection

1. Enter `before replace me now after` in the target field.
2. Select only `replace me now`.
3. Start dictation.
4. Speak `Phrase A`.
5. Confirm only the selected text is replaced.

Expected result:

- selection is replaced cleanly
- surrounding text remains unchanged

### Scenario 4: Mid-Line Caret Insert

1. Enter `before after` in the target field.
2. Place the caret between the two words.
3. Start dictation.
4. Speak `Phrase D`.
5. Confirm text is inserted at the caret, not appended elsewhere.

Expected result:

- insertion occurs at the current caret
- spacing is sane for the field type

### Scenario 5: Focus Retention

1. Focus the target field.
2. Start dictation with the intended control mode.
3. While dictation is active, verify the target app remains the intended insertion target.
4. Commit text.

Expected result:

- target field remains the insertion target
- the dictation app does not steal focus in a way that breaks commit

### Scenario 6: Cancel Path

1. Focus the target field.
2. Start dictation.
3. Speak a few words.
4. Cancel dictation instead of committing.
5. Confirm no final text is inserted.

Expected result:

- no committed text after cancel
- no partial junk remains unless the product explicitly allows interim text

## Tier 1 Targets

## Notepad

Field under test:

- main editor surface

Additional steps:

1. Run all shared scenarios.
2. Repeat Scenario 1 with a multiline document and the caret in the middle of a paragraph.

Extra expectations:

- this should be the clean baseline TSF app
- final commit should not require fallback

## Chrome

Fields under test:

- plain `input`
- plain `textarea`
- search box
- login or email field if available

Additional steps:

1. Run all shared scenarios in a plain `textarea`.
2. Repeat Scenario 1 in a single-line `input`.
3. Repeat Scenario 3 in a search box.

Extra expectations:

- `input` and `textarea` should behave as standard TSF targets
- record separately if browser UI surfaces behave differently from page text fields

## Edge

Fields under test:

- plain `input`
- plain `textarea`
- search box

Additional steps:

1. Repeat the Chrome pass in Edge.
2. Compare results directly against Chrome.

Extra expectations:

- results may be similar to Chrome, but do not assume parity

## Firefox

Fields under test:

- plain `input`
- plain `textarea`
- search box

Additional steps:

1. Run all shared scenarios in a plain `textarea`.
2. Repeat Scenario 1 and Scenario 3 in a single-line `input`.

Extra expectations:

- treat Firefox as its own browser family
- note any differences in selection replacement or caret placement

## Slack Desktop

Fields under test:

- main message composer
- thread reply composer
- search box

Additional steps:

1. Run all shared scenarios in the main message composer.
2. Repeat Scenario 1 in a thread reply.
3. Repeat Scenario 1 in the search box.
4. Verify dictation does not accidentally send the message.

Extra expectations:

- message compose should behave like a high-value Electron target
- search may behave differently from compose

## Discord Desktop

Fields under test:

- main message composer
- thread or DM composer
- search box

Additional steps:

1. Run all shared scenarios in the main message composer.
2. Repeat Scenario 1 in a DM or thread composer.
3. Repeat Scenario 1 in the search box.
4. Verify dictation does not trigger slash-command behavior unexpectedly.

Extra expectations:

- compose and search should be recorded separately
- custom shortcut handling may interfere with focus or submission

## Windows Terminal

Surface under test:

- active shell prompt

Additional steps:

1. Start from a normal shell prompt, not a full-screen TUI.
2. Dictate `Terminal A`.
3. Confirm the command appears exactly once without auto-submitting unless intended.
4. Clear the line and repeat with `Terminal B`.
5. Repeat with `Terminal C`.
6. Repeat while connected through SSH if that is an important use case.
7. Repeat while `vim`, `less`, or another full-screen program is active and record the result separately.

Extra expectations:

- terminal insertion is expected to use the fallback path
- bracketed paste behavior must be noted
- shell prompt mode and full-screen TUI mode must not be merged into one result

## Result Template

Copy this block for each app and field under test:

```text
App:
Field:
Control mode:
Insertion mode setting:
Observed insertion path:

Scenario 1 final commit:
Scenario 2 punctuation:
Scenario 3 replace selection:
Scenario 4 mid-line insert:
Scenario 5 focus retention:
Scenario 6 cancel path:

Clipboard changed:
Unexpected submit:
Focus stolen:
Notes:
```

## Exit Criteria For Tier 1

Tier 1 is in good shape when:

- `Notepad`, `Chrome`, `Edge`, `Firefox`, `Slack`, and `Discord` pass final commit reliably
- `Windows Terminal` is reliable through the fallback path
- selection replacement is clean in standard text fields
- focus retention is no longer a common failure
- every flaky result has a named failure mode

## Follow-On Targets

Once Tier 1 is stable, extend the same checklist style to:

- `Google Docs`
- `VS Code`
- `Word`
- `Everything`
- `Windows Search`
