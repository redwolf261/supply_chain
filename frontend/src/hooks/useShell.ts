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

const fromHash = (): PageId => {
  const id = window.location.hash.replace(/^#\/?/, '');
  return PAGES.some((p) => p.id === id) ? (id as PageId) : 'overview';
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
