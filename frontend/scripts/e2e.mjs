// End-to-end test: real C++ server + built frontend + real browser (installed Chrome/Edge via puppeteer-core).
// Follows the workflow in the project specification through the actual UI and fails on any wrong number,
// missing element, page error, or console error.
//   npm run build && npm run e2e            (set SCX_BROWSER to override the browser path)
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import puppeteer from 'puppeteer-core';

const here = dirname(fileURLToPath(import.meta.url));
const frontend = resolve(here, '..');
const root = resolve(frontend, '..');
const serverExe = join(root, 'build', 'backend', 'scx_server.exe');
const shots = process.env.SCX_SHOTS ?? join(frontend, 'e2e-output');
mkdirSync(shots, { recursive: true });

const candidates = [
  process.env.SCX_BROWSER,
  'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
  'C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe',
  'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe',
].filter(Boolean);
const browserPath = candidates.find((p) => existsSync(p));
if (!browserPath) throw new Error('No Chrome/Edge found; set SCX_BROWSER');
if (!existsSync(serverExe)) throw new Error(`Build the C++ server first: ${serverExe}`);
if (!existsSync(join(frontend, 'dist', 'index.html'))) throw new Error('Run npm run build first');

const children = [];
const start = (cmd, args, name) => {
  const child = spawn(cmd, args, { stdio: 'ignore', windowsHide: true });
  child.on('error', (e) => { console.error(`${name} failed to start: ${e.message}`); });
  children.push(child);
};
const stopAll = () => children.forEach((c) => { try { c.kill(); } catch { /* already gone */ } });

let failures = 0;
const check = (name, ok, detail = '') => {
  console.log(`${ok ? '[ ok ]' : '[FAIL]'} ${name}${ok || !detail ? '' : ` -> ${detail}`}`);
  if (!ok) failures++;
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function waitForHttp(url, tries = 80) {
  for (let i = 0; i < tries; i++) { try { if ((await fetch(url)).ok) return; } catch { /* not up yet */ } await sleep(150); }
  throw new Error(`timeout waiting for ${url}`);
}

start(serverExe, ['--port', '8080', '--data-dir', join(root, 'data')], 'scx_server');
start(process.execPath, [join(frontend, 'node_modules', 'vite', 'bin', 'vite.js'), 'preview', '--port', '4173', '--strictPort'], 'vite preview');
let browser;
try {
  await waitForHttp('http://127.0.0.1:8080/api/health');
  await waitForHttp('http://127.0.0.1:4173/');

  browser = await puppeteer.launch({ executablePath: browserPath, headless: true, args: ['--no-sandbox'], defaultViewport: { width: 1440, height: 900 } });
  const page = await browser.newPage();
  const problems = [];
  page.on('pageerror', (e) => problems.push(`pageerror: ${e.message}`));
  let expectedBadRequests = 0;       // a test below deliberately sends one invalid request; Chrome logs its 400 as a console error
  page.on('console', (m) => {
    if (m.type() !== 'error') return;
    if (m.text().includes('status of 400') && expectedBadRequests > 0) { expectedBadRequests--; return; }
    problems.push(`console: ${m.text()}`);
  });

  const text = () => page.evaluate(() => document.body.innerText);
  const waitText = (t, timeout = 15000) => page.waitForFunction((s) => document.body.innerText.includes(s), { timeout }, t);
  const click = async (label, scope = 'button, a') => {
    const ok = await page.evaluate((l, sel) => {
      const el = [...document.querySelectorAll(sel)].find((e) => e.textContent.trim() === l && !e.disabled);
      if (el) el.click();
      return Boolean(el);
    }, label, scope);
    if (!ok) throw new Error(`no enabled control labelled "${label}"`);
  };
  const go = async (route) => { await page.evaluate((r) => { window.location.hash = `#/${r}`; }, route); await sleep(150); };
  const tileValue = (label) => page.evaluate((l) => {
    const tile = [...document.querySelectorAll('.tile')].find((t) => t.querySelector('.label')?.textContent === l);
    return tile?.querySelector('.value')?.textContent ?? null;
  }, label);
  const shot = (name) => page.screenshot({ path: join(shots, `${name}.png`) });

  // 1. open the dashboard
  await page.goto('http://127.0.0.1:4173/', { waitUntil: 'networkidle0' });
  await waitText('C++ engine connected');
  check('1. dashboard opens and reaches the C++ engine', true);
  check('   empty state before loading', (await text()).includes('No scenario loaded'));
  await shot('01-empty');

  // 2. load a scenario (bundled file)
  await click('Bundled file');
  await page.select('#file', 'small_demo');
  await click('Load scenario');
  await waitText('Network utilisation');
  check('2. scenario loaded', (await tileValue('Suppliers')) === '3', `suppliers tile = ${await tileValue('Suppliers')}`);
  check('   healthy network serves all demand', (await tileValue('Unmet demand')) === '0');
  await shot('02-overview');

  // 3. view the supply network
  await go('network');
  await page.waitForSelector('.react-flow__node');
  await sleep(700);
  const counts = await page.evaluate(() => ({ nodes: document.querySelectorAll('.react-flow__node').length, edges: document.querySelectorAll('.react-flow__edge').length }));
  check('3. network graph shows 9 nodes and 10 routes', counts.nodes === 9 && counts.edges === 10, JSON.stringify(counts));
  await page.click('.react-flow__node');
  check('   clicking a node shows its details', (await text()).includes('Selected node'));
  await shot('03-network');

  // 4. trigger supplier failures
  await go('disruptions');
  await page.select('#kind', 'supplier_failure');
  await page.select('#target', 'S1');
  await click('Apply event');
  await waitText('S1 fails'.replace('S1', 'supplier S1'));
  await page.select('#target', 'S2');
  await click('Apply event');
  await waitText('supplier S2 fails');
  const disruptions = await tileValue('Active disruptions');
  const unmetDown = Number((await tileValue('Unmet demand')).replace(/,/g, ''));
  check('4. two supplier failures registered', disruptions === '2' && unmetDown > 0, `disruptions=${disruptions} unmet=${unmetDown}`);
  check('   failed suppliers drawn as failed on the graph', (await page.evaluate(() => document.querySelectorAll('.facility.failed').length)) === 2);
  await shot('04-disrupted');

  // 5. run greedy recovery and apply it
  await go('lab');
  await page.select('#algorithm', 'greedy');
  await click('Run and apply to scenario');
  await waitText('applied to the scenario');
  check('5. greedy recovery executed', (await text()).includes('Activate backup supplier'));
  check('   result shows runtime and measured heap', /peak heap \d/.test(await text()));
  await shot('05-greedy');

  // 6. back to the SAME disrupted state
  await go('network');
  await click('Undo recovery');
  await sleep(500);
  await go('disruptions');
  await sleep(200);
  check('6. reset to the same disrupted state', (await tileValue('Unmet demand')).replace(/,/g, '') === String(unmetDown) && (await tileValue('Active disruptions')) === '2');

  // 7. minimum-cost flow on that state
  await go('lab');
  await page.select('#algorithm', 'min_cost_flow');
  await click('Run');
  await waitText('preview only: scenario unchanged');
  check('7. min-cost flow run as a preview (scenario unchanged)', true);

  // 8. compare
  await go('compare');
  await click('Run comparison');
  await waitText('lowest cost');
  const rows = await page.evaluate(() => document.querySelectorAll('table.data tbody tr').length);
  check('8. comparison table lists all six strategies', (await page.evaluate(() => [...document.querySelectorAll('table.data')[0].querySelectorAll('tbody tr')].length)) === 6, `rows=${rows}`);
  check('   every strategy reported valid', !(await text()).includes('✕ invalid'));
  check('   charts rendered', (await page.evaluate(() => document.querySelectorAll('.recharts-bar-rectangle').length)) >= 6);
  await page.evaluate(() => window.scrollTo(0, 0));
  await shot('08-compare');

  // 9. updated network flows
  await go('network');
  await sleep(500);
  const labels = await page.evaluate(() => [...document.querySelectorAll('.edge-label')].map((e) => e.textContent));
  check('9. network shows per-route flow/capacity labels', labels.length === 10 && labels.some((l) => /^\d+\/\d+ · \$\d/.test(l)), labels.join(' | '));

  // 10. export
  await go('compare');
  check('   comparison survives leaving the page', (await text()).includes('lowest cost'));
  const csvHref = await page.evaluate(() => document.querySelector('a[download]')?.getAttribute('href'));
  const csv = await page.evaluate(async (h) => (await fetch(h)).text(), csvHref);
  check('10. CSV export contains the compared strategies', csv.startsWith('id,timestamp') && csv.includes('greedy') && csv.includes('dijkstra_dispatch'));

  // 11. Experiment Laboratory: run a seeded batch and read the measured results
  // Set a controlled React input the way React expects: native value setter, then an input event.
  const setNumber = (selector, value) => page.$eval(selector, (el, v) => {
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(el, String(v));
    el.dispatchEvent(new Event('input', { bubbles: true }));
  }, value);
  await go('experiments');
  await page.waitForSelector('#exp-runs');
  await setNumber('#exp-runs', 6);
  await setNumber('#exp-seed', 5);
  await click('Run experiments');
  await waitText('Results: 6 runs', 60000);
  const expRows = await page.evaluate(() => [...document.querySelectorAll('table.data')].pop().querySelectorAll('tbody tr').length);
  const expText = await text();
  check('11. experiment batch ran: one result row per selected strategy', expRows === 5, `rows=${expRows}`);
  check('    results show mean ± sd, paired difference and measured heap', expText.includes('±') && /KB|MB|B\b/.test(expText) && !expText.includes('not measured'));
  check('    both charts rendered with error bars', (await page.evaluate(() => document.querySelectorAll('.recharts-bar-rectangle').length)) >= 10 && (await page.evaluate(() => document.querySelectorAll('.recharts-errorBar').length)) > 0);
  const sameSeed = async () => {
    await click('Run experiments');
    await sleep(900);
    return page.evaluate(() => [...document.querySelectorAll('table.data')].pop().querySelectorAll('tbody tr')[1]?.children[2]?.textContent);
  };
  const first = await page.evaluate(() => [...document.querySelectorAll('table.data')].pop().querySelectorAll('tbody tr')[1].children[2].textContent);
  check('    same seed reproduces the same mean cost', (await sameSeed()) === first, first);
  await shot('11-experiments');
  await setNumber('#exp-runs', 500);
  check('    work limit disables the run button and says why', await page.evaluate(() => [...document.querySelectorAll('button')].find((b) => b.textContent.trim() === 'Run experiments').disabled)
    && (await text()).includes('limit 6000'));
  await setNumber('#exp-runs', 5);
  await setNumber('#net-customers', 0);
  expectedBadRequests = 1;
  await click('Run experiments');
  await waitText('customers', 15000);
  check('    server-side validation error is shown, not swallowed', (await page.evaluate(() => document.querySelector('.banner.error')?.textContent ?? '')).length > 5);
  await setNumber('#net-customers', 10);

  // 12. Multi-day simulator: the page must show exactly what the C++ engine returned.
  let multiDay = null;
  const capture = async (response) => {
    if (response.url().endsWith('/api/multiday/run') && response.request().method() === 'POST') {
      try { multiDay = await response.json(); } catch { /* not JSON */ }
    }
  };
  page.on('response', capture);
  await go('multiday');
  await page.waitForSelector('#md-horizon');
  await setNumber('#md-horizon', 12);
  await setNumber('#md-seed', 3);
  await click('Run simulation');
  await waitText('12 days,', 60000);
  await sleep(300);
  page.off('response', capture);
  const mdRows = await page.evaluate(() => [...document.querySelector('[data-testid="multiday-summary"] table.data').querySelectorAll('tbody tr')]
    .map((tr) => [...tr.children].map((td) => td.textContent.trim())));
  check('12. multi-day run: one summary row per policy', multiDay !== null && mdRows.length === multiDay.policies.length && mdRows.length === 6, `rows=${mdRows.length}`);
  const shown = (n) => Number(String(n).replace(/,/g, ''));
  const mdMatches = multiDay !== null && multiDay.policies.every((p, i) => p.skipped ? mdRows[i][1].startsWith('skipped')
    : shown(mdRows[i][1]) === p.totals.totalCost && mdRows[i][5].startsWith(`${p.totals.actions} (`));
  check('    table shows the totals the C++ engine returned', mdMatches, JSON.stringify(mdRows.map((r) => r[1])));
  const recoveryOk = multiDay !== null && multiDay.policies.every((p, i) => p.skipped || (p.recovery.recovered ? /day|never/.test(mdRows[i][6]) : /not recovered|no disruption/.test(mdRows[i][6])));
  check('    recovery time shown only when the definition is met', recoveryOk, mdRows.map((r) => r[6]).join(' | '));
  check('    six timeline charts drawn from the run', (await page.evaluate(() => document.querySelectorAll('.recharts-line').length)) >= 5 * 6 + 3);
  await setNumber('#md-day', 5);
  await waitText('Day 5 under');
  const mdGraph = await page.evaluate(() => document.querySelectorAll('[data-testid="multiday-day"] .facility').length);
  check('    day slider shows that day\'s network', multiDay !== null && mdGraph === multiDay.scenario.nodes.length, `nodes=${mdGraph}`);
  const movingLabels = await page.evaluate(() => [...document.querySelectorAll('[data-testid="multiday-day"] .edge-label')].map((e) => e.textContent));
  check('    routes show units dispatched and still moving', movingLabels.some((l) => /\d+\/\d+ · \d+ moving/.test(l)), movingLabels.slice(0, 4).join(' | '));
  await shot('13-multiday');
  await page.screenshot({ path: join(shots, '13-multiday-full.png'), fullPage: true });
  await setNumber('#md-horizon', 40);
  check('    horizon outside 5-30 disables the run', await page.evaluate(() => [...document.querySelectorAll('button')].find((b) => b.textContent.trim() === 'Run simulation').disabled));
  await setNumber('#md-horizon', 12);

  // extras: algorithm lab visualisation, learning center, dark theme, bad input path
  await go('lab');
  await page.select('#algorithm', 'bfs');
  await click('Run');
  await waitText('Failure impact');
  check('BFS result with failure impact and highlighted graph', (await page.evaluate(() => document.querySelectorAll('.facility.hl').length)) > 0);
  await page.select('#algorithm', 'dijkstra');
  await page.select('#source', 'S3-backup');
  await page.select('#target', 'C1');
  await click('Run');
  await waitText('unreachable from S3-backup');
  check('Dijkstra reports an unreachable target instead of failing', true);
  await go('learn');
  await page.evaluate(() => document.querySelectorAll('details').forEach((d) => { d.open = true; }));
  check('Learning Center shows live complexity from the server', (await text()).includes('O((V + E) log V)'));
  const learnText = await text();
  check('Learning Center also teaches DP, backtracking, merge sort and topological sort', ['Dynamic programming: inventory', 'Backtracking: which backup', 'Divide and conquer: merge sort', 'Topological sort and cycle detection', 'Master Theorem case 2'].every((s) => learnText.includes(s)));
  await shot('12-learning-center');
  await click('light', 'button');
  await go('network');
  await sleep(600);
  check('light theme applied', (await page.evaluate(() => document.documentElement.getAttribute('data-theme'))) === 'light');
  await shot('09-light-network');
  await go('compare');
  await sleep(300);
  await shot('09-light-compare');
  await click('dark', 'button');
  await go('network');
  await sleep(500);
  check('dark theme applied', (await page.evaluate(() => document.documentElement.getAttribute('data-theme'))) === 'dark');
  await shot('10-dark-network');

  check('no page errors or console errors', problems.length === 0, problems.join(' || '));
} catch (error) {
  failures++;
  console.error(`[FAIL] ${error.stack ?? error}`);
} finally {
  if (browser) await browser.close();
  stopAll();
}
console.log(failures === 0 ? '\nAll end-to-end checks passed.' : `\n${failures} check(s) FAILED.`);
process.exit(failures === 0 ? 0 : 1);
