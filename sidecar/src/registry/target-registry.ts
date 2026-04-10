import type { NormalizedEvent } from "../contracts/events.js";
import type {
  AttentionLevel,
  ProjectId,
  TargetDescriptor,
  TargetId,
  TargetListFilter,
  TargetStatus,
} from "../contracts/targets.js";

export interface ProjectSnapshot {
  projectId: ProjectId;
  targets: TargetDescriptor[];
}

function compareTimestampsDescending(left?: string, right?: string): number {
  const leftMs = left ? Date.parse(left) : 0;
  const rightMs = right ? Date.parse(right) : 0;
  return rightMs - leftMs;
}

function attentionWeight(level: AttentionLevel): number {
  switch (level) {
    case "critical":
      return 4;
    case "high":
      return 3;
    case "medium":
      return 2;
    case "low":
    default:
      return 1;
  }
}

function sortTargets(left: TargetDescriptor, right: TargetDescriptor): number {
  const attentionDelta = attentionWeight(right.attention) - attentionWeight(left.attention);
  if (attentionDelta !== 0) {
    return attentionDelta;
  }

  const activityDelta = compareTimestampsDescending(left.lastActivityAt, right.lastActivityAt);
  if (activityDelta !== 0) {
    return activityDelta;
  }

  return left.label.localeCompare(right.label);
}

function nextStatusFromEvent(event: NormalizedEvent, current: TargetStatus): TargetStatus {
  switch (event.kind) {
    case "assistant_final":
    case "command_finished":
    case "tests_passed":
      return "idle";
    case "assistant_progress":
    case "command_started":
      return current === "awaiting_approval" ? current : "running";
    case "approval_required":
      return "awaiting_approval";
    case "waiting_for_input":
      return "awaiting_input";
    case "tests_failed":
      return "blocked";
    case "job_idle":
      return "idle";
    case "session_ended":
      return "finished";
    default:
      return current;
  }
}

export class TargetRegistry {
  private readonly targets = new Map<TargetId, TargetDescriptor>();

  private readonly latestEvents = new Map<TargetId, NormalizedEvent>();

  upsertTarget(target: TargetDescriptor): TargetDescriptor {
    const previous = this.targets.get(target.id);
    const merged: TargetDescriptor = previous == null
      ? target
      : {
          ...previous,
          ...target,
        };

    this.targets.set(merged.id, merged);
    return merged;
  }

  listTargets(filter: TargetListFilter = {}): TargetDescriptor[] {
    return [...this.targets.values()]
      .filter((target) => {
        if (filter.projectId != null && target.projectId !== filter.projectId) {
          return false;
        }
        if (filter.provider != null && target.provider !== filter.provider) {
          return false;
        }
        if (filter.kinds != null && !filter.kinds.includes(target.kind)) {
          return false;
        }
        return true;
      })
      .sort(sortTargets);
  }

  listProjects(): ProjectSnapshot[] {
    const grouped = new Map<ProjectId, TargetDescriptor[]>();

    for (const target of this.targets.values()) {
      const existing = grouped.get(target.projectId);
      if (existing == null) {
        grouped.set(target.projectId, [target]);
      } else {
        existing.push(target);
      }
    }

    return [...grouped.entries()]
      .map(([projectId, targets]) => ({
        projectId,
        targets: [...targets].sort(sortTargets),
      }))
      .sort((left, right) => left.projectId.localeCompare(right.projectId));
  }

  getTarget(targetId: TargetId): TargetDescriptor | undefined {
    return this.targets.get(targetId);
  }

  getLatestEvent(targetId: TargetId): NormalizedEvent | undefined {
    return this.latestEvents.get(targetId);
  }

  getHostShellTarget(targetId: TargetId): TargetDescriptor | undefined {
    const target = this.targets.get(targetId);
    if (target == null) {
      return undefined;
    }

    if (target.kind === "managed_shell" || target.kind === "observed_shell") {
      return target;
    }

    if ("hostShellTargetId" in target && typeof target.hostShellTargetId === "string") {
      return this.targets.get(target.hostShellTargetId);
    }

    return undefined;
  }

  getAttachedAgentTarget(shellTargetId: TargetId): TargetDescriptor | undefined {
    const shellTarget = this.targets.get(shellTargetId);
    if (shellTarget == null || (shellTarget.kind !== "managed_shell" && shellTarget.kind !== "observed_shell")) {
      return undefined;
    }

    if (shellTarget.attachedAgentTargetId != null) {
      return this.targets.get(shellTarget.attachedAgentTargetId);
    }

    return [...this.targets.values()].find((target) => (
      (target.kind === "managed_agent" || target.kind === "observed_agent")
      && target.hostShellTargetId === shellTargetId
    ));
  }

  recordEvent(event: NormalizedEvent): TargetDescriptor | undefined {
    const target = this.targets.get(event.targetId);
    if (target == null) {
      return undefined;
    }

    this.latestEvents.set(event.targetId, event);

    const updated: TargetDescriptor = {
      ...target,
      lastActivityAt: event.occurredAt,
      lastSummary: event.summary,
      status: nextStatusFromEvent(event, target.status),
    };

    this.targets.set(updated.id, updated);
    return updated;
  }

  findPreferredTarget(options: {
    activeProjectId?: ProjectId;
    activeTargetId?: TargetId;
    provider?: TargetDescriptor["provider"];
    kinds?: TargetDescriptor["kind"][];
  }): TargetDescriptor | undefined {
    const preferredKinds = options.kinds;
    const candidates = this.listTargets({
      projectId: options.activeProjectId,
      provider: options.provider,
      kinds: preferredKinds,
    });

    if (options.activeTargetId != null) {
      const active = this.targets.get(options.activeTargetId);
      if (
        active != null
        && (options.provider == null || active.provider === options.provider)
        && (preferredKinds == null || preferredKinds.includes(active.kind))
      ) {
        return active;
      }
    }

    if (candidates.length > 0) {
      return candidates[0];
    }

    return this.listTargets({
      provider: options.provider,
      kinds: preferredKinds,
    })[0];
  }
}
