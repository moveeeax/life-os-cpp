// All accounts in one currency (Money summary): formats and the geometry of
// the line by day. The numbers come converted from the server; nothing is
// converted here.
import { formatAmount } from '@/lib/amounts';

export const SUMMARY_CURRENCIES = ['USD', 'EUR', 'KZT', 'RUB'] as const;
export const SUMMARY_PERIODS = [30, 90, 365] as const;

/** The currency of the Ledger card: the remembered one, or USD once that one fails (no rate). */
export function cardCurrency(remembered: string, rememberedFailed: boolean): string {
  return rememberedFailed ? 'USD' : remembered;
}

const cents = new Intl.NumberFormat('en-US', {
  minimumFractionDigits: 2,
  maximumFractionDigits: 2,
});

/** A total: cents under a thousand, whole units above. */
export const formatTotal = (v: number): string =>
  formatAmount(v, { wholeFrom: 1000, trueMinus: false });

/** A change with its sign in front. */
export const signed = (v: number): string =>
  formatAmount(v, { wholeFrom: 1000, signed: true, zeroSign: '+' });

/** "1 USD = 490.00 KZT" (@p rate is units of @p currency per 1 @p target), turned around when under one. */
export function rateText(rate: number, currency: string, target: string): string {
  if (currency === target) return '—';
  // Read in the direction where the number is at least one.
  return rate >= 1
    ? `1 ${target} = ${cents.format(rate)} ${currency}`
    : `1 ${currency} = ${cents.format(1 / rate)} ${target}`;
}

export interface Box {
  width: number;
  height: number;
  left: number;
  right: number;
  top: number;
  bottom: number;
}

export interface Geometry {
  points: { x: number; y: number }[];
  line: string;
  area: string;
  ticks: { y: number; value: number }[];
}

/** The line of the totals by day inside @p box, with five value ticks. */
export function chartGeometry(days: { total: number }[], box: Box): Geometry {
  if (days.length === 0) return { points: [], line: '', area: '', ticks: [] };
  const values = days.map((d) => d.total);
  let min = Math.min(...values);
  let max = Math.max(...values);
  const pad = (max - min) * 0.15 || Math.max(1, Math.abs(max) * 0.05);
  min -= pad;
  max += pad;
  const plotW = box.width - box.left - box.right;
  const plotH = box.height - box.top - box.bottom;
  const x = (i: number) =>
    days.length === 1 ? box.left + plotW / 2 : box.left + (i / (days.length - 1)) * plotW;
  const y = (v: number) => box.top + (1 - (v - min) / (max - min)) * plotH;
  const points = values.map((v, i) => ({
    x: Math.round(x(i) * 10) / 10,
    y: Math.round(y(v) * 10) / 10,
  }));
  const line = points.map((p, i) => `${i ? 'L' : 'M'}${p.x},${p.y}`).join('');
  const base = box.top + plotH;
  const area = `${line}L${points[points.length - 1].x},${base}L${points[0].x},${base}Z`;
  const ticks = Array.from({ length: 5 }, (_, k) => {
    const value = min + ((max - min) * k) / 4;
    return { y: Math.round(y(value) * 10) / 10, value };
  });
  return { points, line, area, ticks };
}

/** "Oct 10" for a rate day; the year joins when it is not the year of @p today. */
export function rateDay(iso: string, today: string): string {
  const d = new Date(`${iso}T00:00:00Z`);
  const sameYear = iso.slice(0, 4) === today.slice(0, 4);
  return d.toLocaleDateString('en-US', {
    month: 'short',
    day: 'numeric',
    ...(sameYear ? {} : { year: 'numeric' }),
    timeZone: 'UTC',
  });
}
