# Roadmap

## Goal

Build `terminal-buddy` into a voice-first terminal concierge that can:

- capture voice input with a lightweight Windows control surface
- run terminal commands directly in managed shells
- launch and observe Claude/Codex sessions
- summarize work across multiple targets
- rewrite assistant output into speech-friendly text before TTS

The product should route voice requests across shell targets, agent targets, and
project summaries without requiring a manual mode switch.

The key simplifying rule is:

- Buddy owns shells
- agents run inside shells
- shell routing and agent routing are separate layers on top of the same shell
  substrate

## Phase 0: Foundation Baseline

- `CMake` project bootstrapped
- `SDL3` fetched from upstream
- floating widget, tray, and hotkey control surface
- audio capture and local STT plumbing
- terminal-target paste and submit behavior

Exit criteria:

- app still builds and runs cleanly on a fresh machine
- current dictation workflow remains stable while architecture expands

## Phase 1: Product Reframe

- document the shell-first target-router architecture
- define target types: shell, agent, job, project group
- define the shell-to-agent attachment model
- define the routing categories for host commands, target-directed messages,
  fleet summaries, and ambiguous utterances
- define the native-app / sidecar process boundary

Exit criteria:

- product architecture is written down and stable enough to build against
- roadmap reflects the voice-terminal direction instead of a dictation-only app

## Phase 2: Sidecar Skeleton

- add a sidecar process, preferably Node / TypeScript
- add IPC between the native app and the sidecar
- create the initial target registry
- add project and target identity models
- encode shell attachment for managed agents

Exit criteria:

- native app can send transcribed utterances to the sidecar
- sidecar can return structured actions, summaries, and speech payloads
- the schema reflects that agents are attached to shells instead of being a
  separate transport primitive

## Phase 3: Managed Shell Targets

- bring the minimal `winterm` terminal-state pieces into this repo:
  `ConPTY`, `ghostty-vt`, and plain-text snapshots
- create one or more managed shell targets per project
- add PTY lifecycle ownership in Buddy's own codebase
- route direct terminal intents to managed shells
- support direct command execution for common tasks like build, test, and run

Exit criteria:

- Buddy can run shell commands directly without involving Claude/Codex
- command output and job state are visible to the registry
- managed shell snapshots come from Buddy-owned state, not external windows
- managed shell becomes the default execution substrate for later agent work

## Phase 4: Managed Agent Targets

- launch Claude and Codex sessions from Buddy-managed shells
- enroll managed agent targets into the same registry as shells
- attach each managed agent to its host shell target
- route target-directed utterances to the intended agent target
- keep user wording intact when forwarding messages to agents

Exit criteria:

- Buddy can launch a new agent session in the right project and shell
- Buddy can send follow-up voice requests to a chosen agent target
- managed agent identity remains stable across multiple active sessions
- shell state and agent state stay correlated

## Phase 5: Observed Session Adapters

- add adapters for Codex session logs
- add adapters for Claude session logs
- correlate observed sessions to projects and targets where possible
- use observed mode as a fallback when Buddy did not launch the session

Exit criteria:

- Buddy can summarize externally launched Claude/Codex sessions
- observed sessions can surface assistant output, blockers, and approvals

## Phase 6: Spoken Rendering

- normalize target output into product events
- add an online spoken renderer through OpenRouter
- start with `Gemini 2.5 Flash` for low-latency, low-cost rewrite
- add an offline deterministic fallback renderer
- speak only high-value outputs at first: assistant finals, blockers, approvals,
  and command completions

Exit criteria:

- bullet-heavy assistant output sounds natural when spoken
- speech remains concise and useful instead of reading terminal formatting aloud

## Phase 7: Multi-Target Concierge

- add fleet-summary queries such as:
  - "what is everyone doing"
  - "which sessions are blocked"
  - "check in on my WinTerm project"
- add attention scoring so Buddy can prioritize important targets
- group shell, agent, and job targets under project-level summaries

Exit criteria:

- Buddy can summarize several active targets in one spoken response
- summaries distinguish between direct shell work and agent work

## Phase 8: Background Awareness

- add policy for foreground versus background targets
- foreground targets can speak more often
- background targets should mainly speak blockers, approvals, and completions
- reuse ideas from `winterm` for speech policy and replay later, after the main
  routing foundation is solid

Exit criteria:

- background chatter is low
- important events still break through quickly

## Phase 9: Later Work

- wake word / KWS using `npu-voice-c`
- richer speech policy learned from `winterm`
- classifier-backed output categorization
- more reliable observation of externally launched shells
- local spoken rewrite models if quality and runtime allow

## Notes

- Keep the binary small and the idle CPU near zero.
- Keep routing, normalization, spoken rendering, and TTS as separate layers.
- Prefer event-driven tracking over polling where practical.
- Treat managed targets as the primary path and observed targets as a fallback.
- Do not block the product on KWS or the classifier.
