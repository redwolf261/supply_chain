export const money = (value: number): string => value.toLocaleString('en-US', { maximumFractionDigits: 0 });
export const percent = (ratio: number): string => `${(ratio * 100).toFixed(1)}%`;
export const milliseconds = (value: number): string =>
  value < 0.01 ? '<0.01 ms' : value < 10 ? `${value.toFixed(2)} ms` : `${value.toFixed(1)} ms`;

/** Peak heap bytes as KB/MB; null means the server did not measure it (never shown as 0). */
export function memory(bytes: number | null | undefined): string {
  if (bytes === null || bytes === undefined) return 'not measured';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(2)} MB`;
}

export const titleCase = (id: string): string =>
  id.replace(/_/g, ' ').replace(/\b\w/g, (c) => c.toUpperCase());
