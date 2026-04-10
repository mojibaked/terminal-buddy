import type { ProjectId, TargetId, TargetProvider } from "./targets.js";

export const TARGET_ACTIVITY_KINDS = [
  "shell_prompt",
  "agent_session",
  "running_job",
  "project_overview",
  "unknown",
] as const;

export const TERMINAL_SNAPSHOT_SOURCES = [
  "registry_metadata",
  "cli_query",
] as const;

export type TargetActivityKind = (typeof TARGET_ACTIVITY_KINDS)[number];
export type TerminalSnapshotSource = (typeof TERMINAL_SNAPSHOT_SOURCES)[number];

export interface TerminalStateSnapshot {
  targetId: TargetId;
  projectId: ProjectId;
  capturedAt: string;
  activityKind: TargetActivityKind;
  source: TerminalSnapshotSource;
  confidence: number;
  providerHint?: TargetProvider;
  cwd?: string;
  title?: string;
  prompt?: string;
  recentText?: string;
  statusSummary?: string;
}

export function isTargetActivityKind(value: string): value is TargetActivityKind {
  return (TARGET_ACTIVITY_KINDS as readonly string[]).includes(value);
}
