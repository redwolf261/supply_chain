import type { NodeDto, NodeType } from '../types/api';

export const COLUMN_ORDER: NodeType[] = ['supplier', 'factory', 'warehouse', 'distribution_center', 'customer'];
export const COLUMN_WIDTH = 270;
export const ROW_HEIGHT = 96;

export interface Position { x: number; y: number }

/**
 * Layered left-to-right layout: one column per node type that exists (suppliers first, customers last),
 * nodes stacked in id order, each column vertically centred on the tallest one. Backup suppliers sort
 * after regular ones so they sit at the bottom of the supplier column. Pure and deterministic.
 */
export function layoutNodes(nodes: NodeDto[]): Map<number, Position> {
  const columns = COLUMN_ORDER
    .map((type) => nodes
      .filter((n) => n.type === type)
      .sort((a, b) => Number(a.isBackup) - Number(b.isBackup) || a.id - b.id))
    .filter((column) => column.length > 0);
  const tallest = Math.max(0, ...columns.map((c) => c.length));
  const positions = new Map<number, Position>();
  columns.forEach((column, columnIndex) => {
    const offset = ((tallest - column.length) * ROW_HEIGHT) / 2;
    column.forEach((node, row) => positions.set(node.id, { x: columnIndex * COLUMN_WIDTH, y: offset + row * ROW_HEIGHT }));
  });
  return positions;
}

/** Canvas height that fits the tallest column plus padding, within sensible bounds. */
export function suggestedHeight(nodes: NodeDto[]): number {
  const tallest = Math.max(0, ...COLUMN_ORDER.map((type) => nodes.filter((n) => n.type === type).length));
  return Math.min(720, Math.max(380, tallest * ROW_HEIGHT + 160));
}
