import { describe, expect, it } from 'vitest';
import type { ExperimentRecordDto, ExperimentStrategyDto, SummaryDto } from '../types/api';
import { csvField, excludesZero, formatPaired, recordsToCsv, RUN_COLUMNS, SUMMARY_COLUMNS, summaryToCsv } from './experimentExport';

const summary = (mean: number): SummaryDto => ({ n: 5, mean, stddev: 1, min: mean - 1, max: mean + 1, ci95: 0.5 });

describe('csv helpers', () => {
  it('quotes only when needed and doubles embedded quotes', () => {
    expect(csvField('plain')).toBe('plain');
    expect(csvField('a,b')).toBe('"a,b"');
    expect(csvField('say "hi"')).toBe('"say ""hi"""');
    expect(csvField('line\nbreak')).toBe('"line\nbreak"');
    expect(csvField(null)).toBe('');
    expect(csvField(undefined)).toBe('');
    expect(csvField(0)).toBe('0');
    expect(csvField(false)).toBe('false');
  });

  it('writes one row per run with a column for every header, leaving unmeasured values empty', () => {
    const records: ExperimentRecordDto[] = [
      { run: 0, seed: 11, label: 'greedy', skipped: false, nodes: 20, edges: 30, candidateActions: 4, disruptions: 3, actions: 1,
        totalCost: 1234, unmetDemand: 5, totalDemand: 100, fulfilment: 0.95, utilisation: 0.4, runtimeMs: 0.25, peakMemoryBytes: null, feasible: true },
      { run: 0, seed: 11, label: 'bottleneck_aware(lambda=2)', skipped: false, nodes: 20, edges: 30, candidateActions: 4, disruptions: 3, actions: 0,
        totalCost: 2000, unmetDemand: 0, totalDemand: 100, fulfilment: 1, utilisation: 0.5, runtimeMs: 1, peakMemoryBytes: 4096, feasible: true },
      { run: 1, seed: 12, label: 'exact', skipped: true, skipReason: 'too many', nodes: 20, edges: 30, candidateActions: 14, disruptions: 6 },
    ];
    const lines = recordsToCsv(records).trimEnd().split('\n');
    expect(lines).toHaveLength(4);
    expect(lines[0].split(',')).toEqual(RUN_COLUMNS);
    expect(lines[1].split(',')).toHaveLength(RUN_COLUMNS.length);
    expect(lines[1]).toContain('1234');
    expect(lines[1].split(',')[RUN_COLUMNS.indexOf('peak_memory_bytes')]).toBe('');          // not measured is empty, never 0
    expect(lines[2]).toContain('"bottleneck_aware(lambda=2)"'.replace(/"/g, ''));            // parentheses and = need no quoting
    expect(lines[2].split(',')[RUN_COLUMNS.indexOf('peak_memory_bytes')]).toBe('4096');
    const skipped = lines[3].split(',');
    expect(skipped).toHaveLength(RUN_COLUMNS.length);
    expect(skipped[RUN_COLUMNS.indexOf('skipped')]).toBe('1');
    expect(skipped[RUN_COLUMNS.indexOf('total_cost')]).toBe('');                              // a skipped run has no cost
  });

  it('writes the per-strategy summary', () => {
    const s: ExperimentStrategyDto = {
      label: 'greedy', strategy: 'greedy', lambda: 1, completed: 5, skipped: 0, cost: summary(100), unmetDemand: summary(2), fulfilment: summary(0.9),
      utilisation: summary(0.4), runtimeMs: summary(0.5), actions: summary(1), feasibilityRate: 1, meanRegretPercent: 3.5, wins: 4, memoryMeasured: false, peakMemoryBytes: null,
    };
    const lines = summaryToCsv([s]).trimEnd().split('\n');
    expect(lines[0].split(',')).toEqual(SUMMARY_COLUMNS);
    const fields = lines[1].split(',');
    expect(fields).toHaveLength(SUMMARY_COLUMNS.length);
    expect(fields[SUMMARY_COLUMNS.indexOf('cost_mean')]).toBe('100');
    expect(fields[SUMMARY_COLUMNS.indexOf('peak_bytes_mean')]).toBe('');
  });
});

describe('paired difference wording', () => {
  it('formats the interval and flags only differences larger than the noise', () => {
    expect(formatPaired({ meanDifference: -120.54, ci95: 30.2, n: 30 })).toBe('-120.5 ± 30.2');
    expect(formatPaired({ meanDifference: 3, ci95: 0, n: 1 })).toBe('3.0 (1 run)');
    expect(formatPaired({ meanDifference: 0, ci95: 0, n: 0 })).toBe('–');
    expect(excludesZero({ meanDifference: -120, ci95: 30, n: 30 })).toBe(true);
    expect(excludesZero({ meanDifference: -10, ci95: 30, n: 30 })).toBe(false);      // inside the interval: could be noise
    expect(excludesZero({ meanDifference: 50, ci95: 0, n: 1 })).toBe(false);          // one run proves nothing
  });
});
