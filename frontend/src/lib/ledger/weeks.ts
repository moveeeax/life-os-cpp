import { orderWithAdjustments, totalsByCurrency, type CurrencyTotal } from './index';
import type { Transaction } from './types';

export interface Week {
  from: string;
  to: string;
  rows: Transaction[];
  totals: CurrencyTotal[];
}

const iso = (d: Date) =>
  `${d.getUTCFullYear()}-${String(d.getUTCMonth() + 1).padStart(2, '0')}-${String(d.getUTCDate()).padStart(2, '0')}`;

/** Monday to Sunday weeks of a period, cut at its edges, latest first. */
export function periodWeeks(from: string, to: string): { from: string; to: string }[] {
  const out: { from: string; to: string }[] = [];
  const end = Date.parse(`${to}T00:00:00Z`);
  let start = Date.parse(`${from}T00:00:00Z`);
  for (let t = start; t <= end; t += 864e5) {
    const d = new Date(t);
    if (d.getUTCDay() === 0 || t === end) {
      out.push({ from: iso(new Date(start)), to: iso(d) });
      start = t + 864e5;
    }
  }
  return out.reverse();
}

/** The period's rows by week, latest first; a bank recalculation stays with its original. */
export function groupByWeek(rows: Transaction[], from: string, to: string): Week[] {
  const byId = new Map(rows.map((r) => [r.id, r]));
  return periodWeeks(from, to).map((w) => {
    const list = orderWithAdjustments(rows).filter((r) => {
      const day = (r.adjusts_id && byId.get(r.adjusts_id)?.date) || r.date;
      return day >= w.from && day <= w.to;
    });
    // Latest day first inside the week, as the day groups were.
    list.sort((a, b) => {
      const da = (a.adjusts_id && byId.get(a.adjusts_id)?.date) || a.date;
      const db = (b.adjusts_id && byId.get(b.adjusts_id)?.date) || b.date;
      return db.localeCompare(da);
    });
    return { ...w, rows: list, totals: totalsByCurrency(list) };
  });
}

/** The rates a report's as_if used: units per US dollar by currency. */
export type Rates = Record<string, { per_usd: number }>;

/** Amounts in their currencies as one sum in `target`; a currency without a rate makes it partial. */
export function convertSum(
  amounts: { currency: string; amount: number }[],
  rates: Rates,
  target: string,
): { amount: number; partial: boolean } {
  const to = rates[target]?.per_usd;
  let amount = 0;
  let partial = false;
  for (const a of amounts) {
    if (a.amount === 0) continue;
    const from = a.currency === target ? to : rates[a.currency]?.per_usd;
    if (!from || !to) {
      partial = true;
      continue;
    }
    amount += (a.amount / from) * to;
  }
  return { amount, partial };
}

export function median(values: number[]): number | null {
  const v = values.filter((x) => x > 0).sort((a, b) => a - b);
  if (v.length === 0) return null;
  const m = Math.floor(v.length / 2);
  return v.length % 2 ? v[m] : (v[m - 1] + v[m]) / 2;
}

/** Where a month's spend sits on the scale: half the median and less 0, the median 0.5, double and more 1. */
export function tone(value: number, med: number): number {
  if (value <= 0 || med <= 0) return 0;
  return Math.min(1, Math.max(0, (Math.log2(value / med) + 1) / 2));
}

const LO = [238, 240, 255];
const MID = [150, 146, 230];
const HI = [62, 52, 200];
const mix = (a: number[], b: number[], t: number) =>
  a.map((x, i) => Math.round(x + (b[i] - x) * t));

/** The month's fill on the scale, and whether its text should be dark. */
export function toneColor(t: number): { background: string; dark: boolean } {
  const c = t < 0.5 ? mix(LO, MID, t * 2) : mix(MID, HI, (t - 0.5) * 2);
  return { background: `rgb(${c.join(', ')})`, dark: t < 0.55 };
}
