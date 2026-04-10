import type { VoiceUtteranceRequest } from "../contracts/protocol.js";
import type { HostAction, RoutingDecision } from "../contracts/routing.js";
import type { TerminalStateSnapshot } from "../contracts/terminal-state.js";
import type { TargetDescriptor } from "../contracts/targets.js";
import { TargetRegistry } from "../registry/target-registry.js";

const FLEET_META_PATTERNS = [
  "check in on",
  "check-in on",
  "what is everyone doing",
  "what's everyone doing",
  "summarize",
  "summary",
  "which sessions",
  "which projects",
  "what is blocked",
  "what's blocked",
  "what needs me",
  "who needs me",
] as const;

const AGENT_DIRECT_PATTERNS = [
  "ask ",
  "tell ",
  "message ",
] as const;

const HOST_CONTROL_PATTERNS = [
  "launch ",
  "open ",
  "start ",
  "create ",
] as const;

const SHELL_HINTS = [
  "shell",
  "terminal",
  "powershell",
  "command prompt",
  "tab",
] as const;

const ACTION_HINTS = [
  "build",
  "compile",
  "run",
  "test",
  "execute",
] as const;

const COMMAND_PREFIXES = new Set([
  "git",
  "npm",
  "pnpm",
  "yarn",
  "bun",
  "node",
  "python",
  "py",
  "pip",
  "pytest",
  "cargo",
  "go",
  "dotnet",
  "cmake",
  "ctest",
  "ninja",
  "make",
  "uv",
  "claude",
  "codex",
  "code",
  "dir",
  "ls",
  "pwd",
  "whoami",
  "echo",
  "type",
  "cat",
  "rg",
]);

function normalizeUtterance(text: string): string {
  return text.trim().toLowerCase();
}

function includesAny(text: string, patterns: readonly string[]): boolean {
  return patterns.some((pattern) => text.includes(pattern));
}

function inferExplicitProvider(text: string): TargetDescriptor["provider"] | undefined {
  if (text.includes("claude")) {
    return "claude";
  }
  if (text.includes("codex")) {
    return "codex";
  }
  if (includesAny(text, SHELL_HINTS)) {
    return "shell";
  }
  return undefined;
}

function isAgentProvider(provider: TargetDescriptor["provider"] | undefined): provider is "claude" | "codex" {
  return provider === "claude" || provider === "codex";
}

function agentKinds(): TargetDescriptor["kind"][] {
  return ["managed_agent", "observed_agent"];
}

function shellKinds(): TargetDescriptor["kind"][] {
  return ["managed_shell", "observed_shell"];
}

function firstToken(text: string): string {
  const trimmed = text.trim();
  const firstSpace = trimmed.indexOf(" ");
  return (firstSpace === -1 ? trimmed : trimmed.slice(0, firstSpace)).toLowerCase();
}

function stripRoutingVerb(text: string): string {
  const trimmed = text.trim();
  const lower = trimmed.toLowerCase();
  const prefixes = ["run ", "execute ", "launch ", "start "];

  for (const prefix of prefixes) {
    if (lower.startsWith(prefix)) {
      return trimmed.slice(prefix.length).trim();
    }
  }

  return trimmed;
}

function stripShellContextPhrases(text: string): string {
  let stripped = text.trim();
  const suffixes = [
    " in the shell",
    " in shell",
    " in the terminal",
    " in terminal",
    " in the powershell",
    " in powershell",
    " in the command prompt",
  ];

  for (const suffix of suffixes) {
    if (stripped.toLowerCase().endsWith(suffix)) {
      stripped = stripped.slice(0, stripped.length - suffix.length).trim();
      break;
    }
  }

  return stripped;
}

function looksLikeShellCommand(text: string): boolean {
  const trimmed = text.trim();
  if (trimmed === "") {
    return false;
  }

  if (
    trimmed.startsWith(".\\")
    || trimmed.startsWith("./")
    || trimmed.startsWith("..\\")
    || trimmed.startsWith("../")
  ) {
    return true;
  }

  if (COMMAND_PREFIXES.has(firstToken(trimmed))) {
    return true;
  }

  return trimmed.includes(" --") || trimmed.includes(" | ") || trimmed.includes(" && ") || trimmed.includes(" > ");
}

function planHostAction(text: string): HostAction {
  const candidate = stripShellContextPhrases(stripRoutingVerb(text));

  if (looksLikeShellCommand(candidate)) {
    return {
      kind: "inject_text",
      commandText: candidate,
      submit: true,
    };
  }

  return {
    kind: "none",
    reason: "No deterministic shell command was inferred from the utterance.",
  };
}

function planLaunchAgent(provider: "claude" | "codex"): HostAction {
  return {
    kind: "launch_agent",
    agentProvider: provider,
    commandText: provider,
    submit: true,
  };
}

function planAgentForward(text: string, hasTarget: boolean): HostAction {
  if (!hasTarget || text.trim() === "") {
    return {
      kind: "none",
      reason: "No matching agent target is registered for this utterance.",
    };
  }

  return {
    kind: "inject_text",
    commandText: text.trim(),
    submit: true,
  };
}

export interface RoutingSnapshotContext {
  activeSnapshot?: TerminalStateSnapshot | null;
  projectSnapshots?: TerminalStateSnapshot[];
}

function snapshotLooksAgent(snapshot: TerminalStateSnapshot | null | undefined): boolean {
  return snapshot?.activityKind === "agent_session";
}

function snapshotLooksShell(snapshot: TerminalStateSnapshot | null | undefined): boolean {
  return snapshot?.activityKind === "shell_prompt" || snapshot?.activityKind === "running_job";
}

function attachedAgentMatchesProvider(
  target: TargetDescriptor | undefined,
  explicitProvider: TargetDescriptor["provider"] | undefined,
): boolean {
  if (target == null || !agentKinds().includes(target.kind)) {
    return false;
  }

  return explicitProvider == null || explicitProvider === target.provider;
}

function inferLaunchAgentProvider(
  text: string,
  explicitProvider: TargetDescriptor["provider"] | undefined,
): "claude" | "codex" | undefined {
  if (!includesAny(text, HOST_CONTROL_PATTERNS) || !isAgentProvider(explicitProvider)) {
    return undefined;
  }

  return explicitProvider;
}

function selectPreferredAgentTarget(
  request: VoiceUtteranceRequest,
  registry: TargetRegistry,
  context: RoutingSnapshotContext,
  explicitProvider: TargetDescriptor["provider"] | undefined,
): TargetDescriptor | undefined {
  const activeTarget = request.activeTargetId != null ? registry.getTarget(request.activeTargetId) : undefined;
  const attachedAgent = activeTarget != null ? registry.getAttachedAgentTarget(activeTarget.id) : undefined;

  if (attachedAgentMatchesProvider(attachedAgent, explicitProvider)) {
    return attachedAgent;
  }

  if (activeTarget != null && snapshotLooksAgent(context.activeSnapshot)) {
    if (explicitProvider == null || explicitProvider === activeTarget.provider || explicitProvider === context.activeSnapshot?.providerHint) {
      return activeTarget;
    }
  }

  return registry.findPreferredTarget({
    activeProjectId: request.activeProjectId,
    activeTargetId: request.activeTargetId,
    provider: explicitProvider === "shell" ? undefined : explicitProvider,
    kinds: agentKinds(),
  });
}

function selectPreferredShellTarget(
  request: VoiceUtteranceRequest,
  registry: TargetRegistry,
  context: RoutingSnapshotContext,
): TargetDescriptor | undefined {
  const activeTarget = request.activeTargetId != null ? registry.getTarget(request.activeTargetId) : undefined;
  const hostShell = activeTarget != null ? registry.getHostShellTarget(activeTarget.id) : undefined;

  if (activeTarget != null && snapshotLooksShell(context.activeSnapshot)) {
    return activeTarget;
  }

  if (hostShell != null) {
    return hostShell;
  }

  return registry.findPreferredTarget({
    activeProjectId: request.activeProjectId,
    activeTargetId: request.activeTargetId,
    provider: "shell",
    kinds: shellKinds(),
  });
}

function projectSnapshotCounts(context: RoutingSnapshotContext): { shellCount: number; agentCount: number } {
  let shellCount = 0;
  let agentCount = 0;

  for (const snapshot of context.projectSnapshots ?? []) {
    if (snapshotLooksAgent(snapshot)) {
      agentCount += 1;
    } else if (snapshotLooksShell(snapshot)) {
      shellCount += 1;
    }
  }

  return { shellCount, agentCount };
}

export function routeUtterance(
  request: VoiceUtteranceRequest,
  registry: TargetRegistry,
  context: RoutingSnapshotContext = {},
): RoutingDecision {
  const originalText = request.text.trim();
  const text = normalizeUtterance(request.text);
  const explicitProvider = inferExplicitProvider(text);
  const launchAgentProvider = inferLaunchAgentProvider(text, explicitProvider);
  const explicitlyAgentDirected = explicitProvider === "claude" || explicitProvider === "codex";
  const explicitlyShellDirected = explicitProvider === "shell";
  const activeTarget = request.activeTargetId != null ? registry.getTarget(request.activeTargetId) : undefined;
  const attachedActiveAgent = activeTarget != null ? registry.getAttachedAgentTarget(activeTarget.id) : undefined;
  const snapshotCounts = projectSnapshotCounts(context);

  if (includesAny(text, FLEET_META_PATTERNS)) {
    return {
      category: "fleet_meta",
      confidence: 0.94,
      reason: "Utterance asks for a summary or cross-target status check.",
      selectedProjectId: request.activeProjectId,
    };
  }

  if (launchAgentProvider != null) {
    const shellTarget = selectPreferredShellTarget(request, registry, context);

    return {
      category: "host_control",
      confidence: shellTarget != null ? 0.94 : 0.68,
      reason: shellTarget != null
        ? `Utterance requests launching ${launchAgentProvider} in a shell Buddy controls.`
        : `Utterance requests launching ${launchAgentProvider}, but no preferred shell target is registered yet.`,
      selectedProjectId: shellTarget?.projectId ?? request.activeProjectId,
      selectedTargetId: shellTarget?.id,
      selectedTargetLabel: shellTarget?.label,
      hostAction: planLaunchAgent(launchAgentProvider),
    };
  }

  if (includesAny(text, AGENT_DIRECT_PATTERNS) || explicitlyAgentDirected) {
    const target = selectPreferredAgentTarget(request, registry, context, explicitProvider);

    return {
      category: "target_directed",
      confidence: target != null ? 0.92 : 0.62,
      reason: target != null
        ? "Utterance explicitly references an agent target."
        : "Utterance appears agent-directed, but no matching agent target is registered.",
      selectedProjectId: target?.projectId ?? request.activeProjectId,
      selectedTargetId: target?.id,
      selectedTargetLabel: target?.label,
      hostAction: planAgentForward(originalText, target != null),
    };
  }

  if (explicitlyShellDirected || includesAny(text, HOST_CONTROL_PATTERNS)) {
    const target = selectPreferredShellTarget(request, registry, context);

    return {
      category: "host_control",
      confidence: target != null ? 0.89 : 0.66,
      reason: "Utterance looks like a host-level shell or launch instruction.",
      selectedProjectId: target?.projectId ?? request.activeProjectId,
      selectedTargetId: target?.id,
      selectedTargetLabel: target?.label,
      hostAction: planHostAction(originalText),
    };
  }

  if (includesAny(text, ACTION_HINTS)) {
    const shellTarget = selectPreferredShellTarget(request, registry, context);
    const agentTarget = selectPreferredAgentTarget(request, registry, context, explicitProvider);

    if (explicitlyShellDirected && shellTarget != null) {
      return {
        category: "host_control",
        confidence: 0.88,
        reason: "Action verb detected and the utterance explicitly asks for the shell path.",
        selectedProjectId: shellTarget.projectId,
        selectedTargetId: shellTarget.id,
        selectedTargetLabel: shellTarget.label,
        hostAction: planHostAction(originalText),
      };
    }

    if (
      attachedAgentMatchesProvider(attachedActiveAgent, explicitProvider)
      && !explicitlyShellDirected
    ) {
      return {
        category: "target_directed",
        confidence: 0.84,
        reason: "Action verb detected and the active shell is already hosting an agent.",
        selectedProjectId: attachedActiveAgent?.projectId ?? request.activeProjectId,
        selectedTargetId: attachedActiveAgent?.id,
        selectedTargetLabel: attachedActiveAgent?.label,
        hostAction: planAgentForward(originalText, attachedActiveAgent != null),
      };
    }

    if ((snapshotLooksShell(context.activeSnapshot) || (activeTarget != null && shellKinds().includes(activeTarget.kind))) && !explicitlyAgentDirected) {
      return {
        category: "host_control",
        confidence: 0.83,
        reason: context.activeSnapshot != null
          ? `Action verb detected and the active target snapshot (${context.activeSnapshot.activityKind}) looks shell-like.`
          : "Action verb detected and the active target is a shell target.",
        selectedProjectId: shellTarget?.projectId ?? activeTarget?.projectId ?? request.activeProjectId,
        selectedTargetId: shellTarget?.id ?? activeTarget?.id,
        selectedTargetLabel: shellTarget?.label ?? activeTarget?.label,
        hostAction: planHostAction(originalText),
      };
    }

    if ((snapshotLooksAgent(context.activeSnapshot) || (activeTarget != null && agentKinds().includes(activeTarget.kind))) && !explicitlyShellDirected) {
      return {
        category: "target_directed",
        confidence: 0.81,
        reason: context.activeSnapshot != null
          ? "Action verb detected and the active target snapshot looks like an agent session."
          : "Action verb detected and the active target is an agent target.",
        selectedProjectId: agentTarget?.projectId ?? activeTarget?.projectId ?? request.activeProjectId,
        selectedTargetId: agentTarget?.id ?? activeTarget?.id,
        selectedTargetLabel: agentTarget?.label ?? activeTarget?.label,
        hostAction: planAgentForward(originalText, true),
      };
    }

    if (snapshotCounts.shellCount > 0 && snapshotCounts.agentCount === 0 && shellTarget != null) {
      return {
        category: "host_control",
        confidence: 0.76,
        reason: "Project snapshots only show shell-like targets right now.",
        selectedProjectId: shellTarget.projectId,
        selectedTargetId: shellTarget.id,
        selectedTargetLabel: shellTarget.label,
        hostAction: planHostAction(originalText),
      };
    }

    if (snapshotCounts.agentCount > 0 && snapshotCounts.shellCount === 0 && agentTarget != null) {
      return {
        category: "target_directed",
        confidence: 0.74,
        reason: "Project snapshots only show agent-like targets right now.",
        selectedProjectId: agentTarget.projectId,
        selectedTargetId: agentTarget.id,
        selectedTargetLabel: agentTarget.label,
        hostAction: planAgentForward(originalText, true),
      };
    }

    if (shellTarget != null && agentTarget == null) {
      return {
        category: "host_control",
        confidence: 0.77,
        reason: "Action verb detected and only shell-like targets are available in context.",
        selectedProjectId: shellTarget.projectId,
        selectedTargetId: shellTarget.id,
        selectedTargetLabel: shellTarget.label,
        hostAction: planHostAction(originalText),
      };
    }

    if (shellTarget == null && agentTarget != null) {
      return {
        category: "target_directed",
        confidence: 0.73,
        reason: "Action verb detected and only agent-like targets are available in context.",
        selectedProjectId: agentTarget.projectId,
        selectedTargetId: agentTarget.id,
        selectedTargetLabel: agentTarget.label,
        hostAction: planAgentForward(originalText, true),
      };
    }

    return {
      category: "ambiguous",
      confidence: 0.42,
      reason: "The utterance could mean either direct shell execution or a delegated agent request.",
      selectedProjectId: request.activeProjectId,
    };
  }

  return {
    category: "ambiguous",
    confidence: 0.3,
    reason: "The utterance does not yet match a strong host, target, or fleet summary pattern.",
    selectedProjectId: request.activeProjectId,
  };
}
