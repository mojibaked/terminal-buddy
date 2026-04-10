# terminal-buddy sidecar

This workspace scaffolds the sidecar described in
[docs/product-architecture.md](../docs/product-architecture.md).

It is responsible for:

- target registration
- utterance routing
- project and target summaries
- normalized event intake
- spoken-rendering orchestration

The target model is shell-first:

- Buddy-owned shells are the execution substrate
- agent targets are attached to shells
- routing decides between direct shell execution, shell-attached agent turns,
  and summaries

Current status:

- protocol contracts are defined in `src/contracts/`
- in-memory target registry is implemented in `src/registry/`
- routing heuristics live in `src/router/`
- terminal-state adapters live in `src/state/`
- a minimal service entry point lives in `src/service/`

## Scripts

```powershell
npm run typecheck
npm run build
```

## Terminal State Adapters

The sidecar can query richer target state through an external CLI before it
falls back to registry metadata. The CLI adapter is currently used for
window-backed targets.

Environment variables:

- `TB_TERMINAL_STATE_CLI`
  - executable path or command name for the snapshot CLI
- `TB_TERMINAL_STATE_CLI_ARGS`
  - optional JSON array of string arguments, for example
    `["C:\\tools\\session-farmer.exe", "--json"]`
- `TB_TERMINAL_STATE_CLI_TIMEOUT_MS`
  - optional per-query timeout in milliseconds

The sidecar writes one JSON request to the CLI on stdin:

```json
{
  "kind": "target_snapshot_request",
  "target": {
    "id": "observed:shell:win32:1234",
    "kind": "observed_shell",
    "projectId": "winterm",
    "provider": "shell"
  },
  "latestEvent": null
}
```

The CLI should write one JSON object to stdout and exit `0`:

```json
{
  "activityKind": "shell_prompt",
  "confidence": 0.97,
  "providerHint": "shell",
  "cwd": "C:\\Users\\ether\\projects\\winterm",
  "title": "WinTerm shell",
  "prompt": "PS C:\\Users\\ether\\projects\\winterm>",
  "recentText": "Build finished successfully.",
  "statusSummary": "Ready for the next command."
}
```

If the CLI fails, times out, or returns invalid JSON, the sidecar falls back to
the registry metadata adapter.
