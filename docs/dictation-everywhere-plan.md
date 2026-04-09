# Dictation Everywhere Plan

## Goal

Build a Windows dictation product that feels like it "just works with everything."

In practice, that means:

- TSF/IME-style insertion is the primary path for normal text controls.
- The current companion app remains the control surface for audio capture, settings, tray, widget, and fallback UX.
- Non-TSF targets still get explicit fallback behavior instead of being ignored.
- We measure compatibility with a real app matrix, not by assuming one insertion method works everywhere.

## Reality Check

"Works with everything" is not one mechanism.

Windows text entry compatibility is a ladder:

1. TSF text service / IME path for apps that participate in the Windows text stack.
2. Fallback insertion path for apps that do not expose a usable TSF context.
3. Explicit exceptions for cases that are blocked by integrity level, sandboxing, or non-text surfaces.

The right long-term architecture is:

- `TSF-first` for real text entry compatibility.
- `Buddy fallback` for terminals, legacy surfaces, odd controls, and operational escape hatches.

The wrong architecture is trying to make clipboard paste and synthetic keys become universal. They will never be universal.

## Why TSF

Microsoft's model is clear:

- TSF is the system text input framework for language and text services.
- A text service can provide text to TSF-aware applications without needing app-specific knowledge.
- Modern IMEs are expected to use TSF, not IMM32.

Inference from that:

- If we want the dictation feature to behave like a real input method, TSF is the correct foundation.
- The current floating app is still valuable, but it should become the controller and engine host, not the only insertion mechanism.

## End State Architecture

### 1. Core Dictation Engine

Keep the speech stack in a reusable native core, ideally still C:

- audio session state
- STT backend orchestration
- transcript cleanup rules
- settings schema
- logging / metrics

This core must not know about SDL, tray UI, or TSF.

### 2. Control Host App

Evolve the current app into the out-of-process controller:

- tray
- floating widget mode
- hotkey mode
- audio capture
- backend/model management
- user settings
- diagnostics
- fallback insertion path
- IPC server for the text service

This remains the user-visible app.

### 3. TSF Text Service DLL

Add a thin Windows-only layer, likely C++, that is loaded by TSF:

- COM registration
- text service activation/deactivation
- focus/context tracking
- preserved key registration
- composition lifecycle
- final text commit into the active context
- IPC client to the control host

This DLL should stay thin.

Do not put these inside the TSF DLL:

- model loading
- network calls
- SDL
- tray code
- long-running audio capture
- heavy logging dependencies

Reason: the text service runs inside or alongside target app text contexts. It should be small, predictable, and easy to trust.

### 4. Installer / Registration Layer

We will need install and uninstall support for:

- COM registration
- TSF language profile registration
- category registration
- optional preserved key registration metadata
- per-user enable/disable flow

## Proposed Repo Split

One reasonable structure:

- `src/core/`
  - portable dictation engine in C
- `src/app/`
  - current desktop controller app
- `src/tip/`
  - Windows TSF text service DLL in C++
- `src/ipc/`
  - shared protocol types
- `src/install/`
  - registration helpers or installer-specific code

## Implementation Phases

## Compatibility Matrix

This project should track compatibility by app class and by specific app.

Each app should be assigned:

- priority tier
- expected primary insertion path
- expected fallback path
- support target: `goal`, `best effort`, or `out of scope for now`

The matrix below is the starting point.

### Tier 1: Must Work Well

These are the apps that should define the product.

- `Notepad`
  - primary path: `TSF`
  - fallback: none unless TSF is clearly unavailable
  - support target: `goal`
  - notes: simplest baseline for final commit, caret, and selection behavior
- `Chrome` standard text fields
  - primary path: `TSF`
  - fallback: buddy fallback only if TSF context is missing
  - support target: `goal`
  - notes: validate `input`, `textarea`, search boxes, and login fields
- `Edge` standard text fields
  - primary path: `TSF`
  - fallback: buddy fallback only if TSF context is missing
  - support target: `goal`
  - notes: expected to behave similarly to Chrome, but should still be tested separately
- `Firefox` standard text fields
  - primary path: `TSF`
  - fallback: buddy fallback only if TSF context is missing
  - support target: `goal`
  - notes: do not assume Chromium behavior covers Firefox
- `Slack` desktop app
  - primary path: `TSF`
  - fallback: buddy fallback
  - support target: `goal`
  - notes: Electron app; test message composer, thread reply, and search
- `Discord` desktop app
  - primary path: `TSF`
  - fallback: buddy fallback
  - support target: `goal`
  - notes: Electron app; test normal compose box, slash command surface, and search
- `Windows Terminal`
  - primary path: buddy fallback
  - fallback: none beyond terminal-specific insertion logic
  - support target: `goal`
  - notes: treat terminals as a first-class non-TSF surface

### Tier 2: High-Value Rich Editors and Tools

- `Google Docs` in Chrome
  - primary path: `TSF`
  - fallback: buddy fallback only if TSF path is unusable
  - support target: `goal`
  - notes: rich editor; validate caret movement, replacement, and interim composition
- `Google Docs` in Firefox
  - primary path: `TSF`
  - fallback: buddy fallback only if TSF path is unusable
  - support target: `goal`
  - notes: keep separate from Chromium-based browsers
- `X / Twitter` compose box
  - primary path: `TSF`
  - fallback: buddy fallback
  - support target: `goal`
  - notes: simpler than Docs, but still web-rich enough to expose edge cases
- `VS Code`
  - primary path: `TSF`
  - fallback: buddy fallback
  - support target: `goal`
  - notes: code editor behavior matters even if interim composition support is partial
- `Word`
  - primary path: `TSF`
  - fallback: buddy fallback only if needed
  - support target: `goal`
  - notes: important for mainstream editor coverage
- `Everything`
  - primary path: buddy fallback at first
  - fallback: app-specific fallback behavior if needed
  - support target: `goal`
  - notes: treat as a problem app until proven otherwise

### Tier 3: System and Shell Surfaces

- `Windows Search`
  - primary path: `TSF`
  - fallback: none unless a safe system-supported fallback is available
  - support target: `best effort`
  - notes: useful validation target, but not a first implementation anchor
- `Run dialog`
  - primary path: `TSF`
  - fallback: none unless safe and predictable
  - support target: `best effort`
  - notes: useful sanity check for shell text entry
- elevated admin editor or terminal
  - primary path: matching normal path if integrity level allows it
  - fallback: explicit failure with explanation
  - support target: `best effort`
  - notes: integrity level mismatch should be treated as a product-state decision, not a bug

### Tier 4: Custom Input Surfaces and Games

- `RuneScape` chatbox
  - primary path: assume buddy fallback if anything
  - fallback: explicit unsupported state
  - support target: `out of scope for now`
  - notes: games often use custom input surfaces, raw input, or anti-cheat-sensitive paths
- other games or fullscreen overlays
  - primary path: none by default
  - fallback: explicit unsupported state
  - support target: `out of scope for now`
  - notes: do not let game support define the core architecture

### Per-App Test Data

For every app in the matrix, record:

- `final_commit`: works, flaky, or broken
- `interim_composition`: works, partial, or unavailable
- `selection_replace`: works, flaky, or broken
- `focus_retention`: clean, steals focus, or loses target
- `fallback_needed`: never, sometimes, or always
- `elevated_behavior`: same, blocked, or untested
- `notes`: one or two lines with exact failure mode

## Phase 0: Define Compatibility Targets

Before more code, define the first real matrix.

Must-test apps:

- Windows Terminal
- Chrome text fields
- Firefox text fields
- Slack
- Discord
- Google Docs
- Notepad
- VS Code
- Everything
- Word or another Office editor
- Windows Search
- elevated admin app

For each target, capture:

- final text commit works or not
- interim composition works or not
- caret/selection behavior
- focus stealing
- clipboard mutation
- elevated/non-elevated behavior

Exit criteria:

- a checked-in compatibility matrix exists
- a checked-in compatibility checklist exists
- failure modes are named, not guessed

## Phase 1: Extract a Stable Core

Refactor the current app so transcription logic is callable as a library.

Deliverables:

- reusable dictation session API
- reusable cleanup API
- settings API shared by app and TSF layer
- no SDL dependency in the core

Exit criteria:

- current app still works
- the TSF layer can call the same dictation logic through IPC without duplicating backend code

## Phase 2: Introduce an IPC Boundary

Build a narrow protocol between the control host and the future text service.

Operations should be small and explicit:

- start dictation
- stop dictation
- cancel dictation
- stream status updates
- return final text
- return error state

Probable transport choices:

- named pipe
- local COM server
- lightweight RPC

Recommendation:

- start with a named pipe

Exit criteria:

- the control host can be driven without UI
- one process can request a dictation session and receive a final result

## Phase 3: Build the Minimal TSF Skeleton

Implement the minimum text service shell.

Core responsibilities:

- register the text service and language profile
- implement activation/deactivation
- observe focus/context changes
- register a preserved key for dictation start/stop

Likely interfaces and APIs:

- `ITfTextInputProcessorEx`
- `ITfThreadMgr`
- thread/context focus sinks
- `ITfKeystrokeMgr::PreserveKey`

Important product choice:

- use TSF preserved keys for the real long-term hotkey inside text contexts
- keep the current global hotkey in the control app as a transitional tool and fallback

Exit criteria:

- text service installs and activates
- preserved key reaches our service
- focused text context can be identified reliably

## Phase 4: Final Text Commit Only

Do not start with live composition.

First make final dictation commit work:

- trigger dictation
- receive final transcript from the control host
- open a TSF edit session
- insert final text at the current selection

Likely insertion path:

- `ITfContext`
- `ITfEditSession`
- `ITfInsertAtSelection::InsertTextAtSelection`

Exit criteria:

- final text commit works in the initial target matrix for normal text fields
- clipboard is not required for TSF-capable apps
- no focus stealing is needed for TSF-capable apps

## Phase 5: Live Composition

Once final commit is stable, add interim text:

- create composition range
- update composition as transcript evolves
- commit on finalize
- cancel cleanly on abort

Optional early features:

- display attributes for dictated-but-not-final text
- light visual state in the target field

Exit criteria:

- partial dictation updates are visible where supported
- final commit and cancel paths are correct

## Phase 6: Control Surface Integration

Merge the current app with the TSF path at the product level.

Behavior:

- widget mode can still start/stop dictation
- hotkey mode can still operate globally
- TSF mode becomes the preferred insertion path when a TSF context exists
- fallback path stays available for non-TSF targets

Settings to add:

- control mode: widget / hotkey
- insertion mode: auto / TSF preferred / fallback only
- per-app overrides

Exit criteria:

- the product behaves consistently regardless of how dictation started
- the insertion path is visible in logs

## Phase 7: Fallback Strategy for Non-TSF Targets

TSF is the main path, not the only path.

Fallback categories:

- terminals and consoles
- custom non-text surfaces
- elevated targets blocked by integrity mismatch
- legacy controls that do not provide a usable TSF context

Fallback operations:

- current clipboard + paste path
- app-specific shortcuts where justified
- explicit "no compatible text target" UX when neither path is safe

Exit criteria:

- Windows Terminal still works well
- the app can explain why it fell back
- the app can explain why it failed

## Phase 8: Packaging, Trust, and Recovery

Shipping a text service is not just coding.

We need:

- clean installer and uninstall
- per-user registration first
- clear enable/disable story
- crash containment
- logs that distinguish host failures from text-service failures
- code signing before broad testing

Exit criteria:

- install/uninstall leaves the system clean
- turning the text service off is easy
- failures do not leave text input broken system-wide

## Phase 9: Compatibility Closure

Drive the matrix until the product deserves the claim.

Priority order:

1. browsers and standard editors
2. Office and rich text editors
3. code editors
4. search tools like Everything
5. terminals
6. elevated apps

Per-app outcomes:

- native TSF path
- fallback path
- blocked / unsupported with reason

Exit criteria:

- we can name which path is used for each tested app
- regressions are caught by repeatable manual scripts

## Compatibility Strategy

### Default Rule

If there is a valid TSF text context, use TSF.

### Fallback Rule

If there is no valid TSF context, use the buddy fallback path.

### Failure Rule

If neither is safe, fail loudly and explain why.

Examples:

- "No focused text context"
- "Target app is elevated"
- "Target blocked clipboard paste"
- "Dictation started outside a text field"

## What We Should Not Do

- Do not run the STT model inside the TSF DLL.
- Do not make clipboard paste the primary architecture.
- Do not claim "everything" after only testing Terminal and Chrome.
- Do not tie dictation control to one UI surface.
- Do not assume TSF removes all app-specific work.

## Key Risks

### 1. TSF Does Not Equal Universal Success

Some apps expose poor or partial text contexts.

Mitigation:

- keep fallback insertion
- keep a per-app compatibility matrix

### 2. In-Process Blast Radius

A buggy text service can affect app text input.

Mitigation:

- keep the TSF DLL thin
- keep heavy work out of process

### 3. Elevated / Secure Targets

Integrity boundaries will still matter.

Mitigation:

- detect and report
- decide explicitly whether an elevated companion is in scope

### 4. Language / IME UX Complexity

Installing a text service changes how the system sees input methods.

Mitigation:

- start with per-user opt-in
- provide a clear on/off switch

## Definition of Done

We can reasonably say "just works with everything" only when:

- TSF commit works in mainstream Windows text fields
- the control experience is simple and reliable
- terminals still work through fallback
- failures are rare, named, and diagnosable
- the product no longer relies on clipboard paste for normal apps

## Recommended Immediate Next Steps

1. Freeze the current buddy app as the control host baseline.
2. Extract the dictation core behind a stable API.
3. Add a checked-in compatibility matrix document and checklist.
4. Build a minimal TSF text service DLL that only activates, tracks focus, and registers a preserved key.
5. Make final-text commit work before attempting live composition.

Current checklist:

- [Compatibility Checklist](./compatibility-checklist.md)

## References

Official Microsoft documentation used to shape this plan:

- Text Services Framework overview:
  - https://learn.microsoft.com/en-us/windows/win32/tsf/what-is-text-services-framework
- Why use TSF:
  - https://learn.microsoft.com/en-us/windows/win32/tsf/why-use-text-services-framework
- IME requirements on modern Windows:
  - https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements
- Text service registration:
  - https://learn.microsoft.com/en-us/windows/win32/tsf/text-service-registration
- Text service activation:
  - https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itftextinputprocessor-activate
- Preserved keys:
  - https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfkeystrokemgr-preservekey
- Insert text at selection:
  - https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfinsertatselection
  - https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinsertatselection-inserttextatselection
