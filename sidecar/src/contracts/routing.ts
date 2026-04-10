import type { ProjectId, TargetId } from "./targets.js";

export const UTTERANCE_CATEGORIES = [
  "host_control",
  "target_directed",
  "fleet_meta",
  "ambiguous",
] as const;

export const HOST_ACTION_KINDS = [
  "none",
  "inject_text",
  "launch_agent",
] as const;

export type UtteranceCategory = (typeof UTTERANCE_CATEGORIES)[number];
export type HostActionKind = (typeof HOST_ACTION_KINDS)[number];

export interface HostActionNone {
  kind: "none";
  reason: string;
}

export interface HostActionInjectText {
  kind: "inject_text";
  commandText: string;
  submit: boolean;
}

export interface HostActionLaunchAgent {
  kind: "launch_agent";
  agentProvider: "claude" | "codex";
  commandText: string;
  submit: boolean;
}

export type HostAction = HostActionNone | HostActionInjectText | HostActionLaunchAgent;

export interface RoutingDecision {
  category: UtteranceCategory;
  confidence: number;
  reason: string;
  selectedProjectId?: ProjectId;
  selectedTargetId?: TargetId;
  selectedTargetLabel?: string;
  hostAction?: HostAction;
}

export function isUtteranceCategory(value: string): value is UtteranceCategory {
  return (UTTERANCE_CATEGORIES as readonly string[]).includes(value);
}
