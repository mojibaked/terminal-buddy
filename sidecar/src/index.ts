import * as readline from "node:readline";
import type { BuddyResponse } from "./contracts/protocol.js";
import { parseBuddyRequestEnvelope } from "./contracts/protocol.js";
import { SidecarService, seedDemoState, spokenRenderPreview } from "./service/sidecar-service.js";

function writeJsonLine(message: BuddyResponse): void {
  process.stdout.write(`${JSON.stringify(message)}\n`);
}

async function runStdioServer(): Promise<void> {
  const service = new SidecarService();
  const reader = readline.createInterface({
    input: process.stdin,
    crlfDelay: Infinity,
    terminal: false,
  });

  for await (const line of reader) {
    if (line.trim() === "") {
      continue;
    }

    let parsed: unknown;
    try {
      parsed = JSON.parse(line);
    } catch {
      writeJsonLine({
        kind: "error_response",
        code: "invalid_request",
        message: "Request was not valid JSON.",
      });
      continue;
    }

    const request = parseBuddyRequestEnvelope(parsed);
    if (request == null) {
      writeJsonLine({
        kind: "error_response",
        code: "invalid_request",
        message: "Request did not match the Buddy sidecar protocol.",
      });
      continue;
    }

    writeJsonLine(service.handleRequest(request));
  }
}

function runDemo(): void {
  const service = new SidecarService();
  seedDemoState(service);

  const routeResponse = service.handleRequest({
    kind: "voice_utterance_request",
    utteranceId: "demo-utterance-1",
    text: "check in on all sessions",
    occurredAt: new Date().toISOString(),
    activeProjectId: "winterm",
  });

  const summaryResponse = service.handleRequest({
    kind: "project_summary_request",
    projectId: "winterm",
    scope: "attention_only",
  });

  const snapshotResponse = service.handleRequest({
    kind: "target_snapshot_request",
    targetId: "agent:codex:winterm",
  });

  const speechPreview = spokenRenderPreview(service, {
    id: "event-demo-1",
    targetId: "agent:codex:winterm",
    projectId: "winterm",
    kind: "assistant_final",
    occurredAt: new Date().toISOString(),
    summary: "I found the likely false positives and I am preparing a narrower filter pass.",
  });

  console.log("[sidecar] registered targets:", service.registry.listTargets().length);
  console.log("[sidecar] route demo:", JSON.stringify(routeResponse, null, 2));
  console.log("[sidecar] summary demo:", JSON.stringify(summaryResponse, null, 2));
  console.log("[sidecar] snapshot demo:", JSON.stringify(snapshotResponse, null, 2));
  console.log("[sidecar] speech demo:", JSON.stringify(speechPreview, null, 2));
}

const args = new Set(process.argv.slice(2));
if (args.has("--demo")) {
  runDemo();
} else {
  void runStdioServer();
}
