# Product Architecture

## Goal

Evolve `terminal-buddy` from a dictation companion into a voice-first terminal concierge.

The target experience is:

- the user always talks to Buddy
- Buddy can run terminal commands itself when that is the right tool
- Buddy can launch, observe, and message Claude/Codex sessions that run inside
  shells Buddy understands
- Buddy can summarize work across multiple terminals, jobs, and agent sessions
- Buddy can rewrite assistant output into speech-friendly language before TTS

This product should not require the user to manually switch between "host mode"
and "agent mode."

## Product Position

`terminal-buddy` should become the product repo and user-facing shell.

Other repos feed into it:

- `npu-voice-c`
  - native local STT and TTS
- `machina` / `machina-terminal`
  - planning and deterministic runtime concepts
- `winterm`
  - donor repo for terminal-state ownership pieces we should absorb directly:
    ConPTY, `ghostty-vt`, snapshots, later speech policy

`terminal-buddy` owns the user experience, routing logic, and product
integration.

## Core Principles

### 1. Buddy Is The Only Voice Surface

The user talks to Buddy, not directly to individual terminals or agent sessions.

Buddy may route an utterance to a shell target, an agent target, or a summary
pipeline, but the voice entry point stays unified.

### 2. Shell Owns Execution

The shell is the primitive runtime surface:

- PTY / ConPTY
- prompt and process output
- direct command execution
- authoritative terminal state

An agent is a higher-level target layered on top of a shell:

- Claude/Codex runs inside a shell
- Buddy routes conversational turns to the agent adapter
- the agent may decide what shell commands to run

This keeps the model simple:

- `shell` is the transport
- `agent` is the reasoning layer attached to a shell

### 3. Not Every Shell Has An Agent

Some shells are plain command runners, builds, or dev servers.

Some shells host Claude/Codex sessions.

The system should model both explicitly instead of pretending every shell is an
agent conversation.

### 4. Routing And Execution Are Separate

Buddy decides where an utterance belongs.

The selected target performs the work:

- a shell target runs commands
- an agent target receives the message through its attached shell and decides
  how to act
- a summary pipeline reads state and reports back

### 5. Spoken Rendering Is A Separate Layer

Assistant output is often written to be read, not spoken.

We should treat speech-friendly rewriting as its own stage:

1. capture target output
2. classify or normalize it
3. rewrite it for speech
4. send it to TTS

This rewrite stage can use OpenRouter with `Gemini 2.5 Flash` for low-latency,
low-cost conversational rendering.

### 6. Managed Beats Observed

The best experience comes from targets Buddy launches or enrolls itself.

Observed sessions are still useful, especially for Claude/Codex local session
logs, but they should be treated as adapters with weaker guarantees around
liveness, ownership, and correlation.

## Runtime Shape

The recommended runtime is a two-process system:

### 1. Native Buddy App

The native app remains responsible for:

- floating widget / hotkey UX
- tray and settings
- microphone capture
- local STT
- local TTS playback
- lightweight status UI
- IPC client to the sidecar

This process stays focused on responsiveness and audio UX.

### 2. Buddy Sidecar

Add a sidecar process, ideally Node/TypeScript so it can reuse `machina`.

The sidecar is not "the agent." It is the router and registry.

It should own:

- target registry
- project grouping
- PTY and managed shell lifecycle
- managed Claude/Codex session adapters attached to managed shells
- observed session adapters for local JSONL logs
- utterance routing
- host command planning and execution
- summary generation
- spoken rendering via OpenRouter

Managed shell state should live inside the Buddy codebase, not depend on an
external terminal app. The right path is to absorb the minimal `winterm`
terminal-state stack directly into `terminal-buddy`.

## Target Model

Buddy should track targets, not just sessions.

### Target Types

- `managed_shell`
  - a shell PTY Buddy owns and can execute commands in directly
- `managed_agent`
  - a Claude/Codex session Buddy launched inside a shell it owns
- `observed_agent`
  - an externally launched Claude/Codex session discovered through local logs or
    enrollment, optionally correlated to a shell
- `job`
  - a long-running build, test, watcher, or dev server
- `project_group`
  - a logical container for related targets in one repo or workspace

### Shell Attachment

Every managed agent should point back to its host shell.

That means Buddy can answer both of these cleanly:

- "run `npm test` in the shell"
- "ask Codex in that shell to fix the failing tests"

The shell remains the authoritative runtime state. The agent adds provider-aware
message transport and agent-specific observation.

### Target Metadata

Each target should track at least:

- stable target id
- target type
- project id
- provider: `claude`, `codex`, or `shell`
- cwd
- display label
- current status
- last user activity
- last assistant or process summary
- attention level
- transport details
  - PTY handle, process id, session file path, window handle, or adapter state
- for agent targets: attached shell target id

## Routing Model

Each utterance should be classified into one of four buckets.

### 1. Host / Control

Examples:

- "launch Claude in my WinTerm project"
- "open a shell in terminal-buddy"
- "run the tests in the terminal-buddy shell"

Behavior:

- Buddy plans and executes against shell or launch targets
- `machina` can be used here for deterministic host operations
- shell-directed requests should default to Buddy-owned managed shells

### 2. Target-Directed

Examples:

- "ask Codex in WinTerm to build the executable"
- "tell Claude to rerun the tests"

Behavior:

- Buddy selects the intended agent target
- Buddy resolves the shell that hosts that agent
- Buddy forwards the transcribed utterance verbatim
- Buddy does not paraphrase the user request before sending it to the agent

### 3. Fleet / Meta

Examples:

- "check in on all sessions"
- "which projects are blocked"
- "summarize what everybody is doing"

Behavior:

- Buddy reads normalized state across targets
- Buddy returns a concise summary
- the spoken summary goes through the rewrite layer before TTS

### 4. Ambiguous

Examples:

- "build the executable"
- "run the tests again"

Behavior:

- prefer the current project group or most recently active target
- if confidence is low, ask a one-line clarification

## Target Selection Rules

When an utterance is target-directed, selection should follow this order:

1. explicit target mention
2. explicit project mention
3. currently active or foreground project
4. most recently addressed target
5. highest-attention matching target
6. clarification prompt

This keeps the interaction fluid without forcing explicit mode switches.

## Managed Versus Observed Targets

### Managed

Managed targets are the preferred path.

Buddy launches them, owns the transport, and can correlate:

- cwd
- target identity
- process lifecycle
- speech notifications
- session log location
- shell-to-agent attachment

### Observed

Observed targets are still useful and should be supported where practical.

Examples:

- Codex sessions discovered through `C:\Users\ether\.codex\sessions\...`
- Claude sessions discovered through `C:\Users\ether\.claude\projects\...`

Observed mode is good enough for:

- reading assistant output
- summarizing progress
- background check-ins

Observed mode is weaker for:

- confident target ownership
- direct message transport
- window and PTY correlation
- shell-to-agent attachment

## Output Normalization

The sidecar should not speak raw PTY or raw session log text directly.

It should emit normalized events such as:

- `assistant_final`
- `assistant_progress`
- `approval_required`
- `waiting_for_input`
- `tests_passed`
- `tests_failed`
- `command_started`
- `command_finished`
- `job_idle`
- `session_ended`

These events become the internal contract for summaries and speech.

## Spoken Rendering

Spoken rendering is distinct from both routing and classification.

### Input

The renderer should receive:

- normalized event type
- latest assistant text or command result
- short target context
- optional recent turn history
- desired verbosity
- voice style

### Output

The renderer should produce concise, natural, speech-friendly text.

Examples:

- written approval text becomes a short spoken approval request
- a bullet-heavy assistant answer becomes a natural verbal summary
- a multi-session check-in becomes a compact spoken rollup

### Providers

- primary online renderer: OpenRouter with `Gemini 2.5 Flash`
- fallback offline renderer: deterministic cleanup templates

The fallback path matters so the product still functions when network access is
disabled or unavailable.

## Recommended MVP Slice

The first end-to-end product slice should avoid KWS and avoid the classifier.

Build this first:

1. user presses widget or hotkey
2. local STT transcribes the utterance
3. sidecar routes the utterance to shell, shell-attached agent, or summary
4. Buddy either:
   - runs a host command in a managed shell, or
   - forwards the utterance to a managed agent target attached to a shell
5. target output is normalized
6. normalized text is rewritten for speech
7. local TTS speaks the result

For the first shipping loop, speech should focus on:

- assistant final messages
- approvals
- blockers
- command completion

Raw terminal narration can wait until later.

## Repo Direction

Recommended ownership inside `terminal-buddy`:

- native app
  - current C / SDL app
- sidecar
  - new Node / TypeScript integration layer
- docs
  - architecture, target model, and roadmap

Potential future layout:

- `src/`
  - native app
- `sidecar/`
  - routing, PTY, session adapters, rendering
- `docs/`
  - product docs

## Non-Goals For Early Phases

Do not block the product on:

- wake word detection
- the `winterm` classifier
- full conversational rewriting on-device
- universal observation of arbitrary external terminals

Those can all come later after the routing and multi-target foundation is real.
