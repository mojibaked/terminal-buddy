import { spawnSync } from "node:child_process";
import type { TerminalStateSnapshot } from "../contracts/terminal-state.js";
import { isTargetActivityKind } from "../contracts/terminal-state.js";
import type { TargetDescriptor, TargetProvider } from "../contracts/targets.js";
import type { TerminalStateAdapter, TerminalStateAdapterContext } from "./terminal-state-adapter.js";

interface CliSnapshotResponse {
  activityKind?: string;
  confidence?: number;
  providerHint?: TargetProvider;
  cwd?: string;
  title?: string;
  prompt?: string;
  recentText?: string;
  statusSummary?: string;
}

function clampConfidence(value: number | undefined): number {
  if (value == null || Number.isNaN(value)) {
    return 0.85;
  }

  if (value < 0) {
    return 0;
  }
  if (value > 1) {
    return 1;
  }
  return value;
}

function hasWindowTransport(target: TargetDescriptor): boolean {
  return target.transport.transportKind === "window";
}

export class CliTerminalStateAdapter implements TerminalStateAdapter {
  readonly id = "cli-terminal-state";

  constructor(
    private readonly command: string,
    private readonly args: string[],
    private readonly timeoutMs: number,
  ) {}

  supportsTarget(target: TargetDescriptor): boolean {
    return hasWindowTransport(target);
  }

  snapshotTarget(target: TargetDescriptor, context: TerminalStateAdapterContext): TerminalStateSnapshot | null {
    const latestEvent = context.registry.getLatestEvent(target.id) ?? null;
    const child = spawnSync(
      this.command,
      this.args,
      {
        input: JSON.stringify({
          kind: "target_snapshot_request",
          target,
          latestEvent,
        }),
        encoding: "utf-8",
        timeout: this.timeoutMs,
        windowsHide: true,
      },
    );

    if (child.error != null || child.status !== 0 || child.stdout.trim() === "") {
      return null;
    }

    let parsed: CliSnapshotResponse;
    try {
      parsed = JSON.parse(child.stdout) as CliSnapshotResponse;
    } catch {
      return null;
    }

    if (parsed.activityKind == null || !isTargetActivityKind(parsed.activityKind)) {
      return null;
    }

    return {
      targetId: target.id,
      projectId: target.projectId,
      capturedAt: context.nowIso,
      activityKind: parsed.activityKind,
      source: "cli_query",
      confidence: clampConfidence(parsed.confidence),
      providerHint: parsed.providerHint ?? target.provider,
      cwd: parsed.cwd ?? target.cwd,
      title: parsed.title ?? target.label,
      prompt: parsed.prompt,
      recentText: parsed.recentText,
      statusSummary: parsed.statusSummary ?? target.lastSummary,
    };
  }
}
