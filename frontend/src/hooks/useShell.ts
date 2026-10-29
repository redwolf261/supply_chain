import { useCallback, useEffect, useState } from 'react';

export type ThemeChoice = 'system' | 'light' | 'dark';

// localStorage can throw (private windows, blocked site data), so every access is wrapped and the app
// works without it: the choice just does not persist.
function readStored(): ThemeChoice {
  try {
    const value = localStorage.getItem('scx-theme');
    return value === 'light' || value === 'dark' ? value : 'system';
  } catch { return 'system'; }
}

export function useTheme(): [ThemeChoice, (next: ThemeChoice) => void] {
  const [choice, setChoice] = useState<ThemeChoice>(readStored);
  useEffect(() => {
    const root = document.documentElement;
    if (choice === 'system') root.removeAttribute('data-theme');
    else root.setAttribute('data-theme', choice);
  }, [choice]);
  const update = useCallback((next: ThemeChoice) => {
    setChoice(next);
    try { localStorage.setItem('scx-theme', next); } catch { /* not persisted */ }
  }, []);
  return [choice, update];
}

export const PAGES = [
  { id: 'overview', label: 'Overview', icon: '▦' },
  { id: 'network', label: 'Supply Network', icon: '◎' },
  { id: 'disruptions', label: 'Disruption Simulator', icon: '⚡' },
  { id: 'lab', label: 'Algorithm Lab', icon: '⚗' },
  { id: 'compare', label: 'Comparison', icon: '⇄' },
  { id: 'experiments', label: 'Experiment Lab', icon: '∑' },
  { id: 'multiday', label: 'Multi-day Simulator', icon: '▤' },
  { id: 'learn', label: 'Learning Center', icon: '✎' },
] as const;
export type PageId = (typeof PAGES)[number]['id'];

export interface SubView {
  id: PageId;
  label: string;
  icon: string;
}

export interface Workspace {
  id: string;
  label: string;
  icon: string;
  defaultPage: PageId;
  views: SubView[];
}

export const WORKSPACES: Workspace[] = [
  {
    id: 'operations',
    label: 'Network Operations',
    icon: '🌐',
    defaultPage: 'network',
    views: [
      { id: 'network', label: 'Topology Graph', icon: '◎' },
      { id: 'disruptions', label: 'Disruption Studio', icon: '⚡' },
      { id: 'overview', label: 'Metrics Overview', icon: '▦' },
    ],
  },
  {
    id: 'algorithms',
    label: 'Optimization & Algorithms',
    icon: '⚡',
    defaultPage: 'compare',
    views: [
      { id: 'compare', label: 'Strategy Comparison', icon: '⇄' },
      { id: 'lab', label: 'Algorithm Explorer', icon: '⚗' },
    ],
  },
  {
    id: 'simulations',
    label: 'Advanced Simulations',
    icon: '🔬',
    defaultPage: 'multiday',
    views: [
      { id: 'multiday', label: 'Multi-Day Timeline', icon: '▤' },
      { id: 'experiments', label: 'Batch Experiments', icon: '∑' },
    ],
  },
  {
    id: 'learn',
    label: 'Theory & Guide',
    icon: '📖',
    defaultPage: 'learn',
    views: [
      { id: 'learn', label: 'Learning Center', icon: '✎' },
    ],
  },
];

export function findWorkspaceForPage(page: PageId): Workspace {
  return WORKSPACES.find((w) => w.views.some((v) => v.id === page)) ?? WORKSPACES[0];
}

const fromHash = (): PageId => {
  const id = window.location.hash.replace(/^#\/?/, '');
  return PAGES.some((p) => p.id === id) ? (id as PageId) : 'network';
};

/** Minimal hash router: #/network etc. Keeps the back button working without a routing dependency. */
export function usePage(): PageId {
  const [page, setPage] = useState<PageId>(fromHash);
  useEffect(() => {
    const onChange = () => setPage(fromHash());
    window.addEventListener('hashchange', onChange);
    return () => window.removeEventListener('hashchange', onChange);
  }, []);
  return page;
}
