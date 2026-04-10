import type { TerminalStateSnapshot } from "../contracts/terminal-state.js";
import type { TargetDescriptor } from "../contracts/targets.js";
import type { TerminalStateAdapter, TerminalStateAdapterContext } from "./terminal-state-adapter.js";

function inferActivityKind(target: TargetDescriptor): TerminalStateSnapshot["activityKind"] {
  switch (target.kind) {
    case "managed_agent":
    case "observed_agent":
      return "agent_session";
    case "managed_shell":
    case "observed_shell":
      return target.status === "idle" || target.status === "awaiting_input"
        ? "shell_prompt"
        : "running_job";
    case "job":
      return "running_job";
    case "project_group":
      return "project_overview";
    default:
      return "unknown";
  }
}

function inferConfidence(target: TargetDescriptor): number {
  switch (target.kind) {
    case "managed_agent":
    case "managed_shell":
      return 0.65;
    case "observed_agent":
    case "observed_shell":
      return 0.5;
    case "job":
      return 0.7;
    case "project_group":
      return 0.45;
    default:
      return 0.3;
  }
}

export class RegistryMetadataTerminalStateAdapter implements TerminalStateAdapter {
  readonly id = "registry-metadata";

  supportsTarget(_target: TargetDescriptor): boolean {
    return true;
  }

  snapshotTarget(target: TargetDescriptor, context: TerminalStateAdapterContext): TerminalStateSnapshot | null {
    return {
      targetId: target.id,
      projectId: target.projectId,
      capturedAt: context.nowIso,
      activityKind: inferActivityKind(target),
      source: "registry_metadata",
      confidence: inferConfidence(target),
      providerHint: target.provider,
      cwd: target.cwd,
      title: target.label,
      recentText: target.lastSummary,
      statusSummary: target.lastSummary,
    };
  }
}
