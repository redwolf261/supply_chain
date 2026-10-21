import type { ExperimentRecordDto, ExperimentResponse, ExperimentStrategyDto } from '../types/api';

/** Quotes a CSV field when needed (commas, quotes, line breaks) and doubles embedded quotes. */
export function csvField(value: string | number | boolean | null | undefined): string {
  if (value === null || value === undefined) return '';
  const text = String(value);
  return /[",\n\r]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
}

const row = (fields: Array<string | number | boolean | null | undefined>) => fields.map(csvField).join(',');

export const RUN_COLUMNS = ['run', 'seed', 'strategy', 'skipped', 'nodes', 'edges', 'candidate_actions', 'disruptions', 'actions',
  'total_cost', 'unmet_demand', 'total_demand', 'fulfilment', 'utilisation', 'runtime_ms', 'peak_memory_bytes', 'feasible'];

/** One CSV row per (run, strategy), exactly the measured values; skipped runs keep their reason out of the numbers. */
export function recordsToCsv(records: ExperimentRecordDto[]): string {
  const lines = [RUN_COLUMNS.join(',')];
  for (const r of records) {
    lines.push(row([r.run, r.seed, r.label, r.skipped ? 1 : 0, r.nodes, r.edges, r.candidateActions, r.disruptions, r.actions,
      r.totalCost, r.unmetDemand, r.totalDemand, r.fulfilment, r.utilisation, r.runtimeMs, r.peakMemoryBytes ?? null,
      r.skipped ? null : r.feasible === undefined ? null : r.feasible ? 1 : 0]));
  }
  return lines.join('\n') + '\n';
}

export const SUMMARY_COLUMNS = ['strategy', 'runs_completed', 'runs_skipped', 'cost_mean', 'cost_sd', 'cost_min', 'cost_max', 'cost_ci95',
  'unmet_mean', 'fulfilment_mean', 'utilisation_mean', 'runtime_ms_mean', 'peak_bytes_mean', 'feasibility_rate', 'regret_percent_mean', 'wins'];

export function summaryToCsv(strategies: ExperimentStrategyDto[]): string {
  const lines = [SUMMARY_COLUMNS.join(',')];
  for (const s of strategies) {
    lines.push(row([s.label, s.completed, s.skipped, s.cost.mean, s.cost.stddev, s.cost.min, s.cost.max, s.cost.ci95,
      s.unmetDemand.mean, s.fulfilment.mean, s.utilisation.mean, s.runtimeMs.mean, s.peakMemoryBytes?.mean ?? null,
      s.feasibilityRate, s.meanRegretPercent, s.wins]));
  }
  return lines.join('\n') + '\n';
}

/** Mean difference with its 95% interval as text, e.g. "-120.5 ± 30.2". Negative = the strategy is cheaper than the reference. */
export function formatPaired(d: { meanDifference: number; ci95: number; n: number }): string {
  if (d.n < 2) return d.n === 1 ? `${d.meanDifference.toFixed(1)} (1 run)` : '–';
  return `${d.meanDifference.toFixed(1)} ± ${d.ci95.toFixed(1)}`;
}

/** True when the 95% interval of a paired difference excludes zero (evidence of a real difference, not noise). */
export const excludesZero = (d: { meanDifference: number; ci95: number; n: number }) =>
  d.n >= 2 && Math.abs(d.meanDifference) > d.ci95;

export function responseFilename(response: ExperimentResponse, extension: string): string {
  return `experiment-${response.kind}-x${response.x}-${response.runs}runs.${extension}`;
}
