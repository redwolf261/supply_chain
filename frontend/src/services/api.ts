import type {
  AlgorithmInfo, CompareResponse, ExperimentResponse, LoadRequest, MultiDayResponse, ResultRow, RunResponse, ScenarioList, Snapshot,
} from '../types/api';

/** Error from the server (status > 0) or from failing to reach it at all (status 0). */
export class ApiError extends Error {
  constructor(public status: number, public code: string, message: string) {
    super(message);
    this.name = 'ApiError';
  }
}

type FetchFn = (input: string, init?: RequestInit) => Promise<Response>;

/**
 * Typed client for the C++ server. `fetchFn` and `base` are parameters so tests can substitute a fake
 * server; in the app the defaults talk to the same origin (Vite proxies /api to port 8080).
 */
export function createApi(fetchFn: FetchFn = (i, init) => fetch(i, init), base = '') {
  async function request<T>(method: 'GET' | 'POST', path: string, body?: unknown): Promise<T> {
    let response: Response;
    try {
      response = await fetchFn(`${base}${path}`, {
        method,
        headers: body === undefined ? undefined : { 'Content-Type': 'application/json' },
        body: body === undefined ? undefined : JSON.stringify(body),
      });
    } catch {
      throw new ApiError(0, 'unreachable',
        'Cannot reach the SupplyChainX server. Start it with: build\\backend\\scx_server.exe --data-dir data');
    }
    const text = await response.text();
    let json: unknown = null;
    try { json = text ? JSON.parse(text) : null; } catch { /* non-JSON body handled below */ }
    if (!response.ok) {
      const err = (json as { error?: { code?: string; message?: string } } | null)?.error;
      throw new ApiError(response.status, err?.code ?? 'http_error', err?.message ?? `Server returned HTTP ${response.status}`);
    }
    return json as T;
  }

  return {
    health: () => request<{ status: string; version: string; scenarioLoaded: boolean; memoryTrackingActive: boolean }>('GET', '/api/health'),
    algorithms: async () => (await request<{ algorithms: AlgorithmInfo[] }>('GET', '/api/algorithms')).algorithms,
    scenarios: () => request<ScenarioList>('GET', '/api/scenarios'),
    loadScenario: (source: LoadRequest) => request<Snapshot>('POST', '/api/scenario/load', source),
    reset: (to: 'initial' | 'disrupted') => request<Snapshot>('POST', '/api/scenario/reset', { to }),
    network: () => request<Snapshot>('GET', '/api/network'),
    applyEvent: (event: Record<string, unknown>) => request<Snapshot>('POST', '/api/disruption/apply', event),
    stepEvent: () => request<Snapshot>('POST', '/api/disruption/apply', { step: true }),
    run: (body: Record<string, unknown>) => request<RunResponse>('POST', '/api/algorithm/run', body),
    compare: (body: Record<string, unknown>) => request<CompareResponse>('POST', '/api/algorithms/compare', body),
    runExperiment: (body: Record<string, unknown>) => request<ExperimentResponse>('POST', '/api/experiments/run', body),
    runMultiDay: (body: Record<string, unknown>) => request<MultiDayResponse>('POST', '/api/multiday/run', body),
    results: async () => (await request<{ results: ResultRow[] }>('GET', '/api/results')).results,
    resultsCsvUrl: () => `${base}/api/results?format=csv`,
  };
}

export type Api = ReturnType<typeof createApi>;
