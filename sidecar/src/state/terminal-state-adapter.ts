import type { TerminalStateSnapshot } from "../contracts/terminal-state.js";
import type { TargetDescriptor, TargetId } from "../contracts/targets.js";
import type { TargetRegistry } from "../registry/target-registry.js";

export interface TerminalStateAdapterContext {
  registry: TargetRegistry;
  nowIso: string;
}

export interface TerminalStateAdapter {
  readonly id: string;
  supportsTarget(target: TargetDescriptor): boolean;
  snapshotTarget(target: TargetDescriptor, context: TerminalStateAdapterContext): TerminalStateSnapshot | null;
}

export class TerminalStateCoordinator {
  private readonly adapters: TerminalStateAdapter[] = [];

  constructor(private readonly registry: TargetRegistry) {}

  registerAdapter(adapter: TerminalStateAdapter): void {
    this.adapters.push(adapter);
  }

  snapshotTarget(targetId: TargetId): TerminalStateSnapshot | null {
    const target = this.registry.getTarget(targetId);
    const context: TerminalStateAdapterContext = {
      registry: this.registry,
      nowIso: new Date().toISOString(),
    };

    if (target == null) {
      return null;
    }

    for (const adapter of this.adapters) {
      if (!adapter.supportsTarget(target)) {
        continue;
      }

      const snapshot = adapter.snapshotTarget(target, context);
      if (snapshot != null) {
        return snapshot;
      }
    }

    return null;
  }

  snapshotProject(projectId: string): TerminalStateSnapshot[] {
    return this.registry
      .listTargets({ projectId })
      .map((target) => this.snapshotTarget(target.id))
      .filter((snapshot): snapshot is TerminalStateSnapshot => snapshot != null);
  }
}
