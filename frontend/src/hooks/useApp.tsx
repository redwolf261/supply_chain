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

export interface SharedConfig {
  suppliers: number;
  backupSuppliers: number;
  warehouses: number;
  distributionCenters: number;
  customers: number;
  edges: number;
  capacityFactor: number;
  demandMin: number;
  demandMax: number;
  seed: number;
  severity: number;
  lambda: number;
  spikePercent: number;
}

export const DEFAULT_SHARED_CONFIG: SharedConfig = {
  suppliers: 4,
  backupSuppliers: 2,
  warehouses: 3,
  distributionCenters: 3,
  customers: 6,
  edges: 0,
  capacityFactor: 1.2,
  demandMin: 20,
  demandMax: 60,
  seed: 1,
  severity: 0.3,
  lambda: 1,
  spikePercent: 50,
};

function extractCountsFromSnapshot(s: Snapshot, prev: SharedConfig): SharedConfig {
  const nodes = s.network?.nodes ?? [];
  const backup = nodes.filter((n) => n.type === 'supplier' && n.isBackup).length;
  const regularSuppliers = nodes.filter((n) => n.type === 'supplier' && !n.isBackup).length;
  const warehouses = nodes.filter((n) => n.type === 'warehouse').length;
  const distributionCenters = nodes.filter((n) => n.type === 'distribution_center').length;
  const customers = nodes.filter((n) => n.type === 'customer').length;
  return {
    ...prev,
    suppliers: regularSuppliers > 0 ? regularSuppliers : prev.suppliers,
    backupSuppliers: backup > 0 ? backup : prev.backupSuppliers,
    warehouses: warehouses > 0 ? warehouses : prev.warehouses,
    distributionCenters: distributionCenters > 0 ? distributionCenters : prev.distributionCenters,
    customers: customers > 0 ? customers : prev.customers,
    seed: s.scenario?.seed !== undefined ? s.scenario.seed : prev.seed,
  };
}

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
  config: SharedConfig;
  updateConfig: (patch: Partial<SharedConfig>) => void;
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
  const [config, setConfig] = useState<SharedConfig>(DEFAULT_SHARED_CONFIG);
  const [busyCount, setBusyCount] = useState(0);
  const [error, setError] = useState<string | null>(null);
  const [backend, setBackend] = useState<'unknown' | 'up' | 'down'>('unknown');
  const [memoryTracking, setMemoryTracking] = useState(false);
  const [catalog, setCatalog] = useState<AlgorithmInfo[]>([]);
  const [scenarios, setScenarios] = useState<ScenarioList | null>(null);
  const [highlight, setHighlight] = useState<Highlight>(NO_HIGHLIGHT);
  const [comparison, setComparison] = useState<StoredComparison | null>(null);
  const mounted = useRef(true);

  const updateConfig = useCallback((patch: Partial<SharedConfig>) => {
    setConfig((prev) => ({ ...prev, ...patch }));
  }, []);

  const applySnapshot = useCallback((next: Snapshot | null) => {
    setSnapshot(next);
    if (next) {
      setConfig((prev) => extractCountsFromSnapshot(next, prev));
    }
  }, []);

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
        if (current) applySnapshot(current);
      }
    })();
    return () => { mounted.current = false; };
  }, [api, guard, applySnapshot]);

  const load = useCallback(async (request: LoadRequest) => {
    if ('generate' in request && request.generate) {
      const g = request.generate;
      updateConfig({
        ...(g.suppliers !== undefined && { suppliers: g.suppliers }),
        ...(g.backupSuppliers !== undefined && { backupSuppliers: g.backupSuppliers }),
        ...(g.warehouses !== undefined && { warehouses: g.warehouses }),
        ...(g.distributionCenters !== undefined && { distributionCenters: g.distributionCenters }),
        ...(g.customers !== undefined && { customers: g.customers }),
        ...(g.edges !== undefined && { edges: g.edges }),
        ...(g.capacityFactor !== undefined && { capacityFactor: g.capacityFactor }),
        ...(g.demandMin !== undefined && { demandMin: g.demandMin }),
        ...(g.demandMax !== undefined && { demandMax: g.demandMax }),
        ...(g.seed !== undefined && { seed: g.seed }),
        ...(request.disruptionSeverity !== undefined && { severity: request.disruptionSeverity }),
      });
    }
    const next = await guard(() => api.loadScenario(request));
    if (next) { applySnapshot(next); setHighlight(NO_HIGHLIGHT); }
    return next !== null;
  }, [api, guard, applySnapshot, updateConfig]);

  const reset = useCallback(async (to: 'initial' | 'disrupted') => {
    const next = await guard(() => api.reset(to));
    if (next) { applySnapshot(next); setHighlight(NO_HIGHLIGHT); }
  }, [api, guard, applySnapshot]);

  const applyEvent = useCallback(async (event: Record<string, unknown>) => {
    const next = await guard(() => api.applyEvent(event));
    if (next) applySnapshot(next);
    return next !== null;
  }, [api, guard, applySnapshot]);

  const step = useCallback(async () => {
    const next = await guard(() => api.stepEvent());
    if (next) applySnapshot(next);
    return next !== null;
  }, [api, guard, applySnapshot]);

  const run = useCallback(async (body: Record<string, unknown>) => {
    const response = await guard(() => api.run(body));
    if (response?.snapshot) applySnapshot(response.snapshot);   // set when the plan was executed
    return response;
  }, [api, guard, applySnapshot]);

  const compare = useCallback((body: Record<string, unknown>) => guard(() => api.compare(body)), [api, guard]);

  const value: AppValue = {
    api, snapshot, busy: busyCount > 0, error, clearError: () => setError(null), backend, memoryTracking,
    catalog, scenarios, highlight, setHighlight, comparison, setComparison, config, updateConfig, load, reset, applyEvent, step, run, compare,
  };
  return <AppContext.Provider value={value}>{children}</AppContext.Provider>;
}

export function useApp(): AppValue {
  const value = useContext(AppContext);
  if (!value) throw new Error('useApp must be used inside <AppProvider>');
  return value;
}
