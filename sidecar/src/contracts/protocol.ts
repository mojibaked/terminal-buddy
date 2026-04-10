import type { NormalizedEvent, SpokenRenderRequest, SpokenRenderResult, SpokenRendererId } from "./events.js";
import type { RoutingDecision } from "./routing.js";
import type { TerminalStateSnapshot } from "./terminal-state.js";
import type { ProjectId, TargetDescriptor, TargetId } from "./targets.js";

export const SIDECAR_PROTOCOL_VERSION = 1 as const;

export interface AppCapabilities {
  localStt: boolean;
  localTts: boolean;
  widget: boolean;
  hotkey: boolean;
}

export interface HandshakeRequest {
  kind: "handshake_request";
  protocolVersion: typeof SIDECAR_PROTOCOL_VERSION;
  appId: string;
  capabilities: AppCapabilities;
}

export interface HandshakeResponse {
  kind: "handshake_response";
  protocolVersion: typeof SIDECAR_PROTOCOL_VERSION;
  accepted: boolean;
  sidecarName: string;
  supportedRenderers: SpokenRendererId[];
}

export interface VoiceUtteranceRequest {
  kind: "voice_utterance_request";
  utteranceId: string;
  text: string;
  occurredAt: string;
  activeProjectId?: ProjectId;
  activeTargetId?: TargetId;
}

export interface VoiceUtteranceResponse {
  kind: "voice_utterance_response";
  utteranceId: string;
  decision: RoutingDecision;
}

export interface TargetUpsertRequest {
  kind: "target_upsert_request";
  target: TargetDescriptor;
}

export interface TargetUpsertResponse {
  kind: "target_upsert_response";
  targetId: TargetId;
  projectId: ProjectId;
}

export interface TargetListRequest {
  kind: "target_list_request";
  projectId?: ProjectId;
}

export interface TargetListResponse {
  kind: "target_list_response";
  targets: TargetDescriptor[];
}

export interface TargetSnapshotRequest {
  kind: "target_snapshot_request";
  targetId: TargetId;
}

export interface TargetSnapshotResponse {
  kind: "target_snapshot_response";
  targetId: TargetId;
  snapshot: TerminalStateSnapshot | null;
}

export interface ProjectSummaryRequest {
  kind: "project_summary_request";
  projectId?: ProjectId;
  scope: "all_targets" | "attention_only";
}

export interface ProjectSummaryResponse {
  kind: "project_summary_response";
  projectId?: ProjectId;
  summary: string;
  targetIds: TargetId[];
}

export interface NormalizedEventPublishRequest {
  kind: "normalized_event_publish_request";
  event: NormalizedEvent;
}

export interface NormalizedEventPublishResponse {
  kind: "normalized_event_publish_response";
  eventId: string;
  targetId: TargetId;
}

export interface SpokenRenderRequestMessage {
  kind: "spoken_render_request";
  request: SpokenRenderRequest;
}

export interface SpokenRenderResponseMessage {
  kind: "spoken_render_response";
  result: SpokenRenderResult;
}

export type SidecarErrorCode =
  | "invalid_request"
  | "unsupported_protocol"
  | "unknown_target"
  | "unknown_project"
  | "unprocessable_utterance";

export interface SidecarErrorResponse {
  kind: "error_response";
  code: SidecarErrorCode;
  message: string;
  requestKind?: BuddyRequest["kind"];
}

export type BuddyRequest =
  | HandshakeRequest
  | VoiceUtteranceRequest
  | TargetUpsertRequest
  | TargetListRequest
  | TargetSnapshotRequest
  | ProjectSummaryRequest
  | NormalizedEventPublishRequest
  | SpokenRenderRequestMessage;

export type BuddyResponse =
  | HandshakeResponse
  | VoiceUtteranceResponse
  | TargetUpsertResponse
  | TargetListResponse
  | TargetSnapshotResponse
  | ProjectSummaryResponse
  | NormalizedEventPublishResponse
  | SpokenRenderResponseMessage
  | SidecarErrorResponse;

type UnknownRecord = Record<string, unknown>;

function isRecord(value: unknown): value is UnknownRecord {
  return typeof value === "object" && value !== null;
}

function hasStringField(record: UnknownRecord, field: string): boolean {
  return typeof record[field] === "string";
}

export function parseBuddyRequestEnvelope(value: unknown): BuddyRequest | null {
  if (!isRecord(value) || typeof value.kind !== "string") {
    return null;
  }

  switch (value.kind) {
    case "handshake_request":
      if (value.protocolVersion !== SIDECAR_PROTOCOL_VERSION || !hasStringField(value, "appId")) {
        return null;
      }
      return value as unknown as BuddyRequest;
    case "voice_utterance_request":
      if (!hasStringField(value, "utteranceId") || !hasStringField(value, "text") || !hasStringField(value, "occurredAt")) {
        return null;
      }
      return value as unknown as BuddyRequest;
    case "target_upsert_request":
      return isRecord(value.target) ? (value as unknown as BuddyRequest) : null;
    case "target_list_request":
      return value as unknown as BuddyRequest;
    case "target_snapshot_request":
      return hasStringField(value, "targetId") ? (value as unknown as BuddyRequest) : null;
    case "project_summary_request":
      return value.scope === "all_targets" || value.scope === "attention_only"
        ? (value as unknown as BuddyRequest)
        : null;
    case "normalized_event_publish_request":
      return isRecord(value.event) ? (value as unknown as BuddyRequest) : null;
    case "spoken_render_request":
      return isRecord(value.request) ? (value as unknown as BuddyRequest) : null;
    default:
      return null;
  }
}
