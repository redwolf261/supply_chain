import { useApp } from './hooks/useApp';
import { PAGES, usePage, useTheme, type ThemeChoice } from './hooks/useShell';
import { ComparePage } from './pages/ComparePage';
import { ExperimentsPage } from './pages/ExperimentsPage';
import { DisruptionPage } from './pages/DisruptionPage';
import { LabPage } from './pages/LabPage';
import { LearnPage } from './pages/LearnPage';
import { MultiDayPage } from './pages/MultiDayPage';
import { NetworkPage } from './pages/NetworkPage';
import { OverviewPage } from './pages/OverviewPage';

const THEMES: ThemeChoice[] = ['system', 'light', 'dark'];

export function App() {
  const page = usePage();
  const [theme, setTheme] = useTheme();
  const { error, clearError, backend, busy, snapshot, memoryTracking } = useApp();

  return (
    <div className="shell">
      <nav className="sidebar" aria-label="Main">
        <div className="brand"><span className="brand-mark" aria-hidden>SX</span>SupplyChainX</div>
        {PAGES.map((p) => (
          <a key={p.id} href={`#/${p.id}`} className={`nav-link ${page === p.id ? 'active' : ''}`} aria-current={page === p.id ? 'page' : undefined}>
            <span aria-hidden>{p.icon}</span>{p.label}
          </a>
        ))}
        <div className="sidebar-foot">
          <div>
            <span className={`status-dot ${backend === 'up' ? 'up' : backend === 'down' ? 'down' : ''}`} />
            {backend === 'up' ? 'C++ engine connected' : backend === 'down' ? 'Engine unreachable' : 'Connecting…'}{busy ? ' · working' : ''}
          </div>
          {backend === 'up' && <div>{memoryTracking ? 'Heap tracking on' : 'Heap tracking off'}</div>}
          {snapshot && <div>Scenario: {snapshot.scenario.name}</div>}
          <div className="seg" role="group" aria-label="Theme">
            {THEMES.map((t) => <button key={t} className={theme === t ? 'on' : ''} aria-pressed={theme === t} onClick={() => setTheme(t)}>{t}</button>)}
          </div>
        </div>
      </nav>

      <main className="main">
        {error && (
          <div className="banner error" role="alert">
            <span>{error}</span>
            <button className="btn small" onClick={clearError}>Dismiss</button>
          </div>
        )}
        {page === 'overview' && <OverviewPage />}
        {page === 'network' && <NetworkPage />}
        {page === 'disruptions' && <DisruptionPage />}
        {page === 'lab' && <LabPage />}
        {page === 'compare' && <ComparePage />}
        {page === 'experiments' && <ExperimentsPage />}
        {page === 'multiday' && <MultiDayPage />}
        {page === 'learn' && <LearnPage />}
      </main>
    </div>
  );
}
