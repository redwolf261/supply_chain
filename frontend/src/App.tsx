import { useApp } from './hooks/useApp';
import { findWorkspaceForPage, usePage, useTheme, WORKSPACES, type ThemeChoice } from './hooks/useShell';
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
  const currentWorkspace = findWorkspaceForPage(page);

  return (
    <div className="shell">
      <nav className="sidebar" aria-label="Main">
        <div className="brand"><span className="brand-mark" aria-hidden>SX</span>SupplyChainX</div>

        <div className="nav-group-title">Workspaces</div>
        {WORKSPACES.map((w) => {
          const isActive = currentWorkspace.id === w.id;
          const targetHash = w.views.some((v) => v.id === page) ? page : w.defaultPage;
          return (
            <a
              key={w.id}
              href={`#/${targetHash}`}
              className={`nav-link ${isActive ? 'active' : ''}`}
              aria-current={isActive ? 'page' : undefined}
            >
              <span aria-hidden>{w.icon}</span>{w.label}
            </a>
          );
        })}

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

        {currentWorkspace.views.length > 1 && (
          <div className="workspace-bar">
            <div className="workspace-info">
              <span className="workspace-icon" aria-hidden>{currentWorkspace.icon}</span>
              <span className="workspace-name">{currentWorkspace.label}</span>
            </div>
            <div className="seg" role="tablist" aria-label={`${currentWorkspace.label} views`}>
              {currentWorkspace.views.map((v) => (
                <a
                  key={v.id}
                  href={`#/${v.id}`}
                  role="tab"
                  aria-selected={page === v.id}
                  className={page === v.id ? 'on' : ''}
                >
                  <span style={{ marginRight: 6 }} aria-hidden>{v.icon}</span>{v.label}
                </a>
              ))}
            </div>
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
