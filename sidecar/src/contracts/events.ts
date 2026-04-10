import type { ProjectId, TargetId } from "./targets.js";

export const NORMALIZED_EVENT_KINDS = [
  "assistant_final",
  "assistant_progress",
  "approval_required",
  "waiting_for_input",
  "tests_passed",
  "tests_failed",
  "command_started",
  "command_finished",
  "job_idle",
  "session_ended",
] as const;

export const SPOKEN_STYLES = [
  "utility",
  "pair",
] as const;

export const SPOKEN_VERBOSITIES = [
  "short",
  "medium",
  "long",
] as const;

export const SPOKEN_RENDERERS = [
  "deterministic",
  "openrouter_gemini_2_5_flash",
] as const;

export type NormalizedEventKind = (typeof NORMALIZED_EVENT_KINDS)[number];
export type SpokenStyle = (typeof SPOKEN_STYLES)[number];
export type SpokenVerbosity = (typeof SPOKEN_VERBOSITIES)[number];
export type SpokenRendererId = (typeof SPOKEN_RENDERERS)[number];

export type EventMetadataValue = string | number | boolean | null;

export interface NormalizedEvent {
  id: string;
  targetId: TargetId;
  projectId: ProjectId;
  kind: NormalizedEventKind;
  occurredAt: string;
  summary: string;
  rawText?: string;
  metadata?: Record<string, EventMetadataValue>;
}

export interface SpokenRenderRequest {
  event: NormalizedEvent;
  style: SpokenStyle;
  verbosity: SpokenVerbosity;
  preferredRenderer?: SpokenRendererId;
  targetLabel?: string;
  projectLabel?: string;
}

export interface SpokenRenderResult {
  renderer: SpokenRendererId;
  text: string;
}

export function isNormalizedEventKind(value: string): value is NormalizedEventKind {
  return (NORMALIZED_EVENT_KINDS as readonly string[]).includes(value);
}
