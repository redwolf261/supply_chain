import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import { ApiError, createApi, type Api } from '../services/api';
import type { AlgorithmInfo, CompareResponse, LoadRequest, RunResponse, ScenarioList, Snapshot } from '../types/api';

/** Nodes and routes the Network page should emphasise (e.g. a BFS visit order or a Dijkstra path). */
export interface Highlight {
  nodeIds: number[];
  edgeIds: number[];
  label: string;
  /** Fade everything not highlighted (default). Recovery plans keep the rest visible because their flows matter. */
  dimOthers?: boolean;
}
export const NO_HIGHLIGHT: Highlight = { nodeIds: [], edgeIds: [], label: '' };

/** A comparison is only meaningful for the network state it was computed on, so it carries that state's key. */
export interface StoredComparison { key: string; response: CompareResponse }

interface AppValue {
  api: Api;
  snapshot: Snapshot | null;
  busy: boolean;
  error: string | null;
  clearError: () => void;
  backend: 'unknown' | 'up' | 'down';
  memoryTracking: boolean;
  catalog: AlgorithmInfo[];
  scenarios: ScenarioList | null;
  highlight: Highlight;
  setHighlight: (h: Highlight) => void;
  comparison: StoredComparison | null;
  setComparison: (c: StoredComparison | null) => void;
  load: (request: LoadRequest) => Promise<boolean>;
  reset: (to: 'initial' | 'disrupted') => Promise<void>;
  applyEvent: (event: Record<string, unknown>) => Promise<boolean>;
  step: () => Promise<boolean>;
  run: (body: Record<string, unknown>) => Promise<RunResponse | null>;
  compare: (body: Record<string, unknown>) => Promise<CompareResponse | null>;
}

const AppContext = createContext<AppValue | null>(null);

export function AppProvider({ children, api: injected }: { children: ReactNode; api?: Api }) {
  const api = useMemo(() => injected ?? createApi(), [injected]);
  const [snapshot, setSnapshot] = useState<Snapshot | null>(null);
  const [busyCount, setBusyCount] = useState(0);
  const [error, setError] = useState<string | null>(null);
  const [backend, setBackend] = useState<'unknown' | 'up' | 'down'>('unknown');
  const [memoryTracking, setMemoryTracking] = useState(false);
  const [catalog, setCatalog] = useState<AlgorithmInfo[]>([]);
  const [scenarios, setScenarios] = useState<ScenarioList | null>(null);
  const [highlight, setHighlight] = useState<Highlight>(NO_HIGHLIGHT);
  const [comparison, setComparison] = useState<StoredComparison | null>(null);
  const mounted = useRef(true);

  /** Runs one server call, tracking the busy flag and turning failures into a visible message. */
  const guard = useCallback(async <T,>(work: () => Promise<T>): Promise<T | null> => {
    setBusyCount((n) => n + 1);
    try {
      const value = await work();
      if (mounted.current) setBackend('up');
      return value;
    } catch (e) {
      if (!mounted.current) return null;
      const err = e instanceof ApiError ? e : new ApiError(-1, 'client_error', e instanceof Error ? e.message : String(e));
      if (err.status === 0) setBackend('down');
      setError(err.message);
      return null;
    } finally {
      if (mounted.current) setBusyCount((n) => n - 1);
    }
  }, []);

  useEffect(() => {
    mounted.current = true;
    (async () => {
      const health = await guard(() => api.health());
      if (!health) return;
      setMemoryTracking(health.memoryTrackingActive);
      const [algorithms, list] = await Promise.all([guard(() => api.algorithms()), guard(() => api.scenarios())]);
      if (algorithms) setCatalog(algorithms);
      if (list) setScenarios(list);
      // The server keeps its scenario across browser reloads, so pick it up if one is loaded.
      if (health.scenarioLoaded) {
        const current = await guard(() => api.network());
        if (current) setSnapshot(current);
      }
    })();
    return () => { mounted.current = false; };
  }, [api, guard]);

  const load = useCallback(async (request: LoadRequest) => {
    const next = await guard(() => api.loadScenario(request));
    if (next) { setSnapshot(next); setHighlight(NO_HIGHLIGHT); }
    return next !== null;
  }, [api, guard]);

  const reset = useCallback(async (to: 'initial' | 'disrupted') => {
    const next = await guard(() => api.reset(to));
    if (next) { setSnapshot(next); setHighlight(NO_HIGHLIGHT); }
  }, [api, guard]);

  const applyEvent = useCallback(async (event: Record<string, unknown>) => {
    const next = await guard(() => api.applyEvent(event));
    if (next) setSnapshot(next);
    return next !== null;
  }, [api, guard]);

  const step = useCallback(async () => {
    const next = await guard(() => api.stepEvent());
    if (next) setSnapshot(next);
    return next !== null;
  }, [api, guard]);

  const run = useCallback(async (body: Record<string, unknown>) => {
    const response = await guard(() => api.run(body));
    if (response?.snapshot) setSnapshot(response.snapshot);   // set when the plan was executed
    return response;
  }, [api, guard]);

  const compare = useCallback((body: Record<string, unknown>) => guard(() => api.compare(body)), [api, guard]);

  const value: AppValue = {
    api, snapshot, busy: busyCount > 0, error, clearError: () => setError(null), backend, memoryTracking,
    catalog, scenarios, highlight, setHighlight, comparison, setComparison, load, reset, applyEvent, step, run, compare,
  };
  return <AppContext.Provider value={value}>{children}</AppContext.Provider>;
}

export function useApp(): AppValue {
  const value = useContext(AppContext);
  if (!value) throw new Error('useApp must be used inside <AppProvider>');
  return value;
}
