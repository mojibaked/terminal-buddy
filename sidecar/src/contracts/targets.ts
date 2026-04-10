export const TARGET_KINDS = [
  "managed_shell",
  "observed_shell",
  "managed_agent",
  "observed_agent",
  "job",
  "project_group",
] as const;

export const TARGET_PROVIDERS = [
  "shell",
  "claude",
  "codex",
  "group",
] as const;

export const TARGET_STATUSES = [
  "idle",
  "running",
  "launching_agent",
  "thinking",
  "awaiting_input",
  "awaiting_approval",
  "blocked",
  "finished",
  "errored",
] as const;

export const ATTENTION_LEVELS = [
  "low",
  "medium",
  "high",
  "critical",
] as const;

export const SHELL_OCCUPANCIES = [
  "utility",
  "agent_host",
] as const;

export type TargetId = string;
export type ProjectId = string;
export type TargetKind = (typeof TARGET_KINDS)[number];
export type TargetProvider = (typeof TARGET_PROVIDERS)[number];
export type TargetStatus = (typeof TARGET_STATUSES)[number];
export type AttentionLevel = (typeof ATTENTION_LEVELS)[number];
export type ShellOccupancy = (typeof SHELL_OCCUPANCIES)[number];

export interface PtyTransport {
  transportKind: "pty";
  ptyId: string;
  processId?: number;
}

export interface SessionLogTransport {
  transportKind: "session_log";
  provider: "claude" | "codex";
  sessionFilePath: string;
  sessionId?: string;
}

export interface WindowTransport {
  transportKind: "window";
  platform: "windows";
  windowId: string;
  processId?: number;
  processName?: string;
}

export interface JobTransport {
  transportKind: "job";
  processId: number;
  command: string;
}

export interface ProjectGroupTransport {
  transportKind: "project_group";
  rootCwd: string;
}

export type TargetTransport =
  | PtyTransport
  | SessionLogTransport
  | WindowTransport
  | JobTransport
  | ProjectGroupTransport;

export interface BaseTargetDescriptor {
  id: TargetId;
  kind: TargetKind;
  projectId: ProjectId;
  provider: TargetProvider;
  cwd: string;
  label: string;
  status: TargetStatus;
  attention: AttentionLevel;
  lastActivityAt?: string;
  lastSummary?: string;
  tags?: string[];
}

export interface ManagedShellTarget extends BaseTargetDescriptor {
  kind: "managed_shell";
  provider: "shell";
  occupancy: ShellOccupancy;
  attachedAgentTargetId?: TargetId;
  transport: PtyTransport;
}

export interface ObservedShellTarget extends BaseTargetDescriptor {
  kind: "observed_shell";
  provider: "shell";
  occupancy?: ShellOccupancy;
  attachedAgentTargetId?: TargetId;
  transport: WindowTransport;
}

export interface ManagedAgentTarget extends BaseTargetDescriptor {
  kind: "managed_agent";
  provider: "claude" | "codex";
  // Agents are shell-backed targets, not a separate execution primitive.
  hostShellTargetId: TargetId;
  transport: PtyTransport | SessionLogTransport | WindowTransport;
}

export interface ObservedAgentTarget extends BaseTargetDescriptor {
  kind: "observed_agent";
  provider: "claude" | "codex";
  // Observed agents may or may not be correlated to a shell yet.
  hostShellTargetId?: TargetId;
  transport: SessionLogTransport | WindowTransport;
}

export interface JobTarget extends BaseTargetDescriptor {
  kind: "job";
  provider: "shell";
  hostShellTargetId?: TargetId;
  transport: JobTransport;
}

export interface ProjectGroupTarget extends BaseTargetDescriptor {
  kind: "project_group";
  provider: "group";
  memberTargetIds: TargetId[];
  transport: ProjectGroupTransport;
}

export type TargetDescriptor =
  | ManagedShellTarget
  | ObservedShellTarget
  | ManagedAgentTarget
  | ObservedAgentTarget
  | JobTarget
  | ProjectGroupTarget;

export interface TargetListFilter {
  projectId?: ProjectId;
  provider?: TargetProvider;
  kinds?: TargetKind[];
}

export function isTargetKind(value: string): value is TargetKind {
  return (TARGET_KINDS as readonly string[]).includes(value);
}

export function isTargetProvider(value: string): value is TargetProvider {
  return (TARGET_PROVIDERS as readonly string[]).includes(value);
}

export function isTargetStatus(value: string): value is TargetStatus {
  return (TARGET_STATUSES as readonly string[]).includes(value);
}

export function isAttentionLevel(value: string): value is AttentionLevel {
  return (ATTENTION_LEVELS as readonly string[]).includes(value);
}

export function isShellOccupancy(value: string): value is ShellOccupancy {
  return (SHELL_OCCUPANCIES as readonly string[]).includes(value);
}
