import type {
  BuddyRequest,
  BuddyResponse,
  HandshakeResponse,
  ProjectSummaryRequest,
  SidecarErrorCode,
  SidecarErrorResponse,
  SpokenRenderRequestMessage,
} from "../contracts/protocol.js";
import { SIDECAR_PROTOCOL_VERSION } from "../contracts/protocol.js";
import type {
  NormalizedEvent,
  SpokenRenderRequest,
  SpokenRenderResult,
} from "../contracts/events.js";
import { SPOKEN_RENDERERS } from "../contracts/events.js";
import type { TerminalStateSnapshot } from "../contracts/terminal-state.js";
import type { TargetDescriptor } from "../contracts/targets.js";
import { routeUtterance } from "../router/intent.js";
import { TargetRegistry } from "../registry/target-registry.js";
import { CliTerminalStateAdapter } from "../state/cli-terminal-state-adapter.js";
import { RegistryMetadataTerminalStateAdapter } from "../state/registry-metadata-terminal-state-adapter.js";
import { TerminalStateCoordinator } from "../state/terminal-state-adapter.js";

function errorResponse(code: SidecarErrorCode, message: string, requestKind?: BuddyRequest["kind"]): SidecarErrorResponse {
  return {
    kind: "error_response",
    code,
    message,
    requestKind,
  };
}

function renderDeterministicSpeech(request: SpokenRenderRequest): SpokenRenderResult {
  const event = request.event;
  const summary = event.summary.trim();
  const targetLead = request.targetLabel != null && request.targetLabel !== ""
    ? `${request.targetLabel}: `
    : "";

  switch (event.kind) {
    case "approval_required":
      return {
        renderer: "deterministic",
        text: `${targetLead}needs your approval. ${summary}`,
      };
    case "tests_passed":
      return {
        renderer: "deterministic",
        text: `${targetLead}tests passed. ${summary}`,
      };
    case "tests_failed":
      return {
        renderer: "deterministic",
        text: `${targetLead}tests failed. ${summary}`,
      };
    case "command_finished":
      return {
        renderer: "deterministic",
        text: `${targetLead}command finished. ${summary}`,
      };
    case "assistant_final":
      return {
        renderer: "deterministic",
        text: `${targetLead}${summary}`,
      };
    case "waiting_for_input":
      return {
        renderer: "deterministic",
        text: `${targetLead}is waiting for input. ${summary}`,
      };
    case "session_ended":
      return {
        renderer: "deterministic",
        text: `${targetLead}session ended. ${summary}`,
      };
    default:
      return {
        renderer: "deterministic",
        text: `${targetLead}${summary}`,
      };
  }
}

function parseCliArgs(rawValue: string | undefined): string[] {
  if (rawValue == null || rawValue.trim() === "") {
    return [];
  }

  try {
    const parsed = JSON.parse(rawValue) as unknown;
    if (!Array.isArray(parsed) || !parsed.every((value) => typeof value === "string")) {
      return [];
    }
    return parsed;
  } catch {
    return [];
  }
}

function activityLead(snapshot: TerminalStateSnapshot | null): string {
  switch (snapshot?.activityKind) {
    case "agent_session":
      return "is in an agent session";
    case "running_job":
      return "is running work";
    case "shell_prompt":
      return "is at a shell prompt";
    case "project_overview":
      return "is acting as a project overview";
    default:
      return "is active";
  }
}

function withSentencePunctuation(text: string): string {
  const trimmed = text.trim();
  if (trimmed === "") {
    return trimmed;
  }

  const lastChar = trimmed.slice(-1);
  return [".", "!", "?"].includes(lastChar) ? trimmed : `${trimmed}.`;
}

function targetSummaryLine(target: TargetDescriptor, snapshot: TerminalStateSnapshot | null): string {
  const lead = activityLead(snapshot);
  const detail = snapshot?.statusSummary ?? snapshot?.recentText ?? target.lastSummary;
  return detail != null && detail.trim() !== ""
    ? `${target.label} ${lead}: ${withSentencePunctuation(detail)}`
    : `${target.label} ${lead}.`;
}

function buildProjectSummary(
  registry: TargetRegistry,
  stateCoordinator: TerminalStateCoordinator,
  request: ProjectSummaryRequest,
): BuddyResponse {
  const targets = request.projectId == null
    ? registry.listTargets()
    : registry.listTargets({ projectId: request.projectId });

  if (request.projectId != null && targets.length === 0) {
    return errorResponse("unknown_project", `No targets are registered for project "${request.projectId}".`, request.kind);
  }

  const filtered = request.scope === "attention_only"
    ? targets.filter((target) => target.attention === "high" || target.attention === "critical")
    : targets;

  const summary = filtered.length === 0
    ? "No high-attention targets right now."
    : filtered
        .map((target) => targetSummaryLine(target, stateCoordinator.snapshotTarget(target.id)))
        .join(" ");

  return {
    kind: "project_summary_response",
    projectId: request.projectId,
    summary,
    targetIds: filtered.map((target) => target.id),
  };
}

export class SidecarService {
  readonly registry = new TargetRegistry();

  readonly stateCoordinator = new TerminalStateCoordinator(this.registry);

  readonly supportedRenderers = [...SPOKEN_RENDERERS];

  constructor() {
    const cliCommand = process.env.TB_TERMINAL_STATE_CLI?.trim();
    const cliArgs = parseCliArgs(process.env.TB_TERMINAL_STATE_CLI_ARGS);
    const timeoutMsRaw = Number(process.env.TB_TERMINAL_STATE_CLI_TIMEOUT_MS ?? "250");
    const timeoutMs = Number.isFinite(timeoutMsRaw) && timeoutMsRaw > 0 ? timeoutMsRaw : 250;

    if (cliCommand != null && cliCommand !== "") {
      this.stateCoordinator.registerAdapter(new CliTerminalStateAdapter(cliCommand, cliArgs, timeoutMs));
    }
    this.stateCoordinator.registerAdapter(new RegistryMetadataTerminalStateAdapter());
  }

  handleRequest(request: BuddyRequest): BuddyResponse {
    switch (request.kind) {
      case "handshake_request": {
        const response: HandshakeResponse = {
          kind: "handshake_response",
          protocolVersion: SIDECAR_PROTOCOL_VERSION,
          accepted: request.protocolVersion === SIDECAR_PROTOCOL_VERSION,
          sidecarName: "terminal-buddy-sidecar",
          supportedRenderers: this.supportedRenderers,
        };
        return response;
      }

      case "voice_utterance_request":
        {
          const activeSnapshot = request.activeTargetId != null
            ? this.stateCoordinator.snapshotTarget(request.activeTargetId)
            : null;
          const projectSnapshots = request.activeProjectId != null
            ? this.stateCoordinator.snapshotProject(request.activeProjectId)
            : [];

        return {
          kind: "voice_utterance_response",
          utteranceId: request.utteranceId,
          decision: routeUtterance(request, this.registry, {
            activeSnapshot,
            projectSnapshots,
          }),
        };
      }

      case "target_upsert_request": {
        const target = this.registry.upsertTarget(request.target);
        return {
          kind: "target_upsert_response",
          targetId: target.id,
          projectId: target.projectId,
        };
      }

      case "target_list_request":
        return {
          kind: "target_list_response",
          targets: this.registry.listTargets({
            projectId: request.projectId,
          }),
        };

      case "target_snapshot_request": {
        const target = this.registry.getTarget(request.targetId);
        if (target == null) {
          return errorResponse("unknown_target", `No target is registered for "${request.targetId}".`, request.kind);
        }
        return {
          kind: "target_snapshot_response",
          targetId: request.targetId,
          snapshot: this.stateCoordinator.snapshotTarget(request.targetId),
        };
      }

      case "project_summary_request":
        return buildProjectSummary(this.registry, this.stateCoordinator, request);

      case "normalized_event_publish_request": {
        const updatedTarget = this.registry.recordEvent(request.event);
        if (updatedTarget == null) {
          return errorResponse("unknown_target", `No target is registered for event target "${request.event.targetId}".`, request.kind);
        }
        return {
          kind: "normalized_event_publish_response",
          eventId: request.event.id,
          targetId: updatedTarget.id,
        };
      }

      case "spoken_render_request":
        return {
          kind: "spoken_render_response",
          result: renderDeterministicSpeech(request.request),
        };

      default:
        return errorResponse("invalid_request", "Unhandled request kind.");
    }
  }
}

export function seedDemoState(service: SidecarService): void {
  service.registry.upsertTarget({
    id: "project:winterm",
    kind: "project_group",
    projectId: "winterm",
    provider: "group",
    cwd: "C:\\Users\\ether\\projects\\winterm",
    label: "WinTerm",
    status: "running",
    attention: "medium",
    memberTargetIds: ["shell:winterm", "agent:codex:winterm"],
    transport: {
      transportKind: "project_group",
      rootCwd: "C:\\Users\\ether\\projects\\winterm",
    },
  });

  service.registry.upsertTarget({
    id: "shell:winterm",
    kind: "managed_shell",
    projectId: "winterm",
    provider: "shell",
    occupancy: "agent_host",
    attachedAgentTargetId: "agent:codex:winterm",
    cwd: "C:\\Users\\ether\\projects\\winterm",
    label: "WinTerm shell",
    status: "launching_agent",
    attention: "low",
    lastSummary: "Preparing a Codex session.",
    transport: {
      transportKind: "pty",
      ptyId: "pty-winterm-1",
    },
  });

  service.registry.upsertTarget({
    id: "agent:codex:winterm",
    kind: "managed_agent",
    projectId: "winterm",
    provider: "codex",
    hostShellTargetId: "shell:winterm",
    cwd: "C:\\Users\\ether\\projects\\winterm",
    label: "Codex in WinTerm",
    status: "thinking",
    attention: "high",
    lastSummary: "Investigating output classification heuristics.",
    transport: {
      transportKind: "session_log",
      provider: "codex",
      sessionFilePath: "C:\\Users\\ether\\.codex\\sessions\\2026\\04\\10\\example.jsonl",
    },
  });
}

export function spokenRenderPreview(service: SidecarService, event: NormalizedEvent): SpokenRenderResult {
  const request: SpokenRenderRequestMessage = {
    kind: "spoken_render_request",
    request: {
      event,
      style: "pair",
      verbosity: "short",
      preferredRenderer: "deterministic",
      targetLabel: service.registry.getTarget(event.targetId)?.label,
    },
  };

  const response = service.handleRequest(request);
  if (response.kind === "error_response") {
    return {
      renderer: "deterministic",
      text: response.message,
    };
  }

  if (response.kind !== "spoken_render_response") {
    return {
      renderer: "deterministic",
      text: "Spoken rendering returned an unexpected response.",
    };
  }

  return response.result;
}
