import { describe, expect, it } from 'vitest';
import { LESSONS } from './learn';

// Ids served by GET /api/algorithms (backend/src/api/ApiService.cpp, catalog()).
const SERVER_CATALOG = ['bfs', 'dfs', 'dijkstra', 'max_flow', 'min_cost_flow', 'dijkstra_dispatch', 'greedy', 'bottleneck_aware', 'bottleneck_aware_lambda0', 'exact'];

describe('Learning Center lessons', () => {
  it('have unique ids and every section filled in', () => {
    expect(new Set(LESSONS.map((l) => l.id)).size).toBe(LESSONS.length);
    for (const l of LESSONS) {
      for (const label of [l.title, l.paradigm]) expect(label.trim().length).toBeGreaterThan(0);
      for (const text of [l.what, l.why, l.where, l.pseudocode, l.cpp, l.dryRun, l.limitations]) expect(text.trim().length).toBeGreaterThan(30);
      expect(l.how.length).toBeGreaterThanOrEqual(3);
    }
  });

  it('show a complexity for every algorithm: from the server when it runs it, otherwise from the lesson', () => {
    for (const l of LESSONS) {
      const fromServer = SERVER_CATALOG.includes(l.id);
      expect(fromServer || l.complexity !== undefined).toBe(true);
      if (fromServer) expect(l.complexity).toBeUndefined();          // one source of truth per algorithm
    }
  });

  it('cover the syllabus modules as well as the runnable algorithms', () => {
    const ids = LESSONS.map((l) => l.id);
    for (const id of ['bfs', 'dfs', 'dijkstra', 'max_flow', 'min_cost_flow', 'greedy', 'bottleneck_aware', 'inventory_dp', 'backtracking', 'merge_sort', 'topological_sort']) {
      expect(ids).toContain(id);
    }
  });
});
