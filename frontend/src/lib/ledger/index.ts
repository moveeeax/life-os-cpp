// Pure logic of the Money pages: formats, periods, per-currency totals, the
// parse draft, small rules. Amounts of different currencies are never added.
import type { Currency, ParseJob, ParseLine, Transaction, TransactionInput } from './types';

export * from './types';

// ── formats ────────────────────────────────────────────────────────────────

/** Decimals of a currency from the user's list (2 when unknown). */
export const decimalsOf = (currencies: Currency[] | undefined, code: string): number =>
  currencies?.find((c) => c.code === code)?.decimals ?? 2;

/** "₸13,275.61"-style text in the currency's own decimals. */
export function formatMoney(amount: number, currency: string, decimals = 2): string {
  try {
    return new Intl.NumberFormat('en-US', {
      style: 'currency',
      currency,
      minimumFractionDigits: decimals,
      maximumFractionDigits: decimals,
    }).format(amount);
  } catch {
    // An unknown code: the number and the code.
    return `${amount.toFixed(decimals)} ${currency}`;
  }
}

/** The amount as it moved the account: expenses negative, incomes positive. */
export function signedAmount(row: Pick<Transaction, 'type' | 'amount'>): number {
  return row.type === 'income' ? row.amount : -row.amount;
}

// ── periods (calendar days in the browser, "YYYY-MM-DD" on the wire) ──────

export type PeriodKind = 'week' | 'month' | 'quarter';

const pad = (n: number) => String(n).padStart(2, '0');
const text = (d: Date) => `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;

function parse(date: string): Date | null {
  const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(date);
  if (!m) return null;
  const d = new Date(Number(m[1]), Number(m[2]) - 1, Number(m[3]));
  return d.getMonth() === Number(m[2]) - 1 && d.getDate() === Number(m[3]) ? d : null;
}

export const todayLocal = (now: Date = new Date()): string => text(now);

export function periodFromSearch(
  search: URLSearchParams,
  today: string,
): { kind: PeriodKind; date: string } {
  const kind = search.get('kind');
  const date = search.get('date');
  return {
    kind: kind === 'week' || kind === 'quarter' || kind === 'month' ? kind : 'month',
    date: date && parse(date) ? date : today,
  };
}

/** The same ranges the server builds: week Monday..Sunday, month, quarter. */
export function periodRange(kind: PeriodKind, date: string): { from: string; to: string } {
  const d = parse(date) ?? new Date();
  if (kind === 'week') {
    const start = new Date(d);
    start.setDate(d.getDate() - ((d.getDay() + 6) % 7));
    const end = new Date(start);
    end.setDate(start.getDate() + 6);
    return { from: text(start), to: text(end) };
  }
  const firstMonth = kind === 'quarter' ? Math.floor(d.getMonth() / 3) * 3 : d.getMonth();
  const start = new Date(d.getFullYear(), firstMonth, 1);
  const end = new Date(d.getFullYear(), firstMonth + (kind === 'quarter' ? 3 : 1), 0);
  return { from: text(start), to: text(end) };
}

/** A day of the period `by` periods away (its first day). */
export function shiftPeriod(kind: PeriodKind, date: string, by: number): string {
  const start = parse(periodRange(kind, date).from) ?? new Date();
  if (kind === 'week') {
    start.setDate(start.getDate() + 7 * by);
  } else {
    start.setMonth(start.getMonth() + (kind === 'quarter' ? 3 : 1) * by);
  }
  return text(start);
}

export function formatDay(date: string): string {
  const d = parse(date);
  return d
    ? d.toLocaleDateString('en-US', { weekday: 'short', month: 'short', day: 'numeric' })
    : date;
}

// ── totals ─────────────────────────────────────────────────────────────────

export interface CurrencyTotal {
  currency: string;
  income: number;
  expense: number;
}

/** Posted rows summed per currency (adjustments count as charges), sorted by code. */
export function totalsByCurrency(rows: Transaction[]): CurrencyTotal[] {
  const map = new Map<string, CurrencyTotal>();
  for (const r of rows) {
    if (r.status !== 'posted') continue;
    const t = map.get(r.currency) ?? { currency: r.currency, income: 0, expense: 0 };
    if (r.type === 'income') t.income += r.amount;
    else t.expense += r.amount;
    map.set(r.currency, t);
  }
  return [...map.values()].sort((a, b) => a.currency.localeCompare(b.currency));
}

export interface Day {
  date: string;
  rows: Transaction[];
  totals: CurrencyTotal[];
}

/**
 * Rows in display order: each bank recalculation right after its original
 * when the original is among them; otherwise in place.
 */
export function orderWithAdjustments(rows: Transaction[]): Transaction[] {
  const ids = new Set(rows.map((r) => r.id));
  const children = new Map<string, Transaction[]>();
  for (const r of rows) {
    if (r.adjusts_id && ids.has(r.adjusts_id)) {
      children.set(r.adjusts_id, [...(children.get(r.adjusts_id) ?? []), r]);
    }
  }
  const out: Transaction[] = [];
  for (const r of rows) {
    if (r.adjusts_id && ids.has(r.adjusts_id)) continue;
    out.push(r, ...(children.get(r.id) ?? []));
  }
  return out;
}

/** Rows by day, newest day first; a recalculation sits under its original's day. */
export function groupByDay(rows: Transaction[]): Day[] {
  const byId = new Map(rows.map((r) => [r.id, r]));
  const map = new Map<string, Transaction[]>();
  for (const r of orderWithAdjustments(rows)) {
    const day = (r.adjusts_id && byId.get(r.adjusts_id)?.date) || r.date;
    map.set(day, [...(map.get(day) ?? []), r]);
  }
  return [...map.entries()]
    .sort((a, b) => b[0].localeCompare(a[0]))
    .map(([date, list]) => ({ date, rows: list, totals: totalsByCurrency(list) }));
}

// ── the parse draft ────────────────────────────────────────────────────────

export interface DraftLine {
  key: string;
  selected: boolean;
  type: 'income' | 'expense';
  date: string;
  time: string | null;
  account_id: string | null;
  amount: string;
  merchant: string;
  name: string;
  category_id: string | null;
  receipt_amount: string;
  receipt_currency: string;
  fx_note: string;
  confidence: number;
  note: string;
}

export function draftFromJob(job: ParseJob, categoryKinds?: Map<string, string>): DraftLine[] {
  return (job.result ?? []).map((l: ParseLine, i) => ({
    key: `${job.id}:${i}`,
    selected: true,
    type: l.type,
    date: l.date,
    time: l.time ?? null,
    account_id: l.account_id,
    amount: String(l.amount),
    merchant: l.merchant,
    name: l.name,
    category_id:
      l.category_id && categoryKinds && categoryKinds.get(l.category_id) !== l.type
        ? null
        : l.category_id,
    receipt_amount: l.receipt_amount === null ? '' : String(l.receipt_amount),
    receipt_currency: l.receipt_currency ?? '',
    fx_note: l.fx_note,
    confidence: l.confidence,
    note: l.note,
  }));
}

const num = (v: string): number | null => {
  const t = v.trim().replace(',', '.');
  if (t === '') return null;
  const n = Number(t);
  return Number.isFinite(n) ? n : null;
};

/** The first thing that keeps the selected lines out of the inbox, or null. */
export function draftProblem(lines: DraftLine[]): string | null {
  for (const [i, l] of lines.entries()) {
    if (!l.selected) continue;
    const at = `Line ${i + 1}`;
    if (!l.account_id) return `${at}: pick the account.`;
    const a = num(l.amount);
    if (a === null || a <= 0) return `${at}: the amount must be above 0.`;
    if (!l.name.trim()) return `${at}: a name is needed.`;
    if (!parse(l.date)) return `${at}: the date is not a calendar day.`;
    if (!l.category_id) return `${at}: pick a category.`;
    const hasReceipt = l.receipt_amount.trim() !== '' || l.receipt_currency.trim() !== '';
    if (
      hasReceipt &&
      ((num(l.receipt_amount) ?? 0) <= 0 || !/^[A-Z]{3}$/.test(l.receipt_currency.trim()))
    )
      return `${at}: the receipt needs an amount and a currency code.`;
  }
  return null;
}

/** A line of POST parse/{id}/accept: the server sets status and source. */
export type AcceptLine = Omit<TransactionInput, 'source' | 'status'>;

/** The selected lines as the body of POST parse/{id}/accept. */
export function linesToAccept(lines: DraftLine[]): AcceptLine[] {
  return lines
    .filter((l) => l.selected)
    .map((l) => ({
      type: l.type,
      date: l.date,
      time: l.time,
      account_id: l.account_id ?? '',
      amount: num(l.amount) ?? 0,
      merchant: l.merchant,
      name: l.name.trim(),
      category_id: l.category_id,
      receipt_amount: l.receipt_amount.trim() === '' ? null : num(l.receipt_amount),
      receipt_currency: l.receipt_currency.trim() === '' ? null : l.receipt_currency.trim(),
      fx_note: l.fx_note,
      note: l.note,
    }));
}

export function parseErrorText(error: string | null | undefined): string {
  if (!error) return 'The parse failed.';
  const colon = error.indexOf(':');
  const code = colon === -1 ? error : error.slice(0, colon);
  const detail = colon === -1 ? '' : error.slice(colon + 1).trim();
  if (code === 'invalid_answer')
    return `The model did not answer with a usable list${detail ? `: ${detail}` : '.'}`;
  if (code === 'not_configured') return 'Parsing is not configured on the server.';
  if (code === 'money_disabled') return 'The money module is switched off on the server.';
  if (code === 'provider_refused') return 'The language model provider refused the request.';
  if (code === 'provider_unavailable')
    return 'The language model provider did not answer. Try again later.';
  if (code === 'parse_timeout') return 'The parse is taking too long. Try again in a minute.';
  const status = /^provider_error_(\d+)$/.exec(code);
  if (status)
    return `The provider answered with an error (${status[1]})${detail ? `: ${detail}` : '.'}`;
  return detail || code;
}

// ── small rules ────────────────────────────────────────────────────────────

/** A transfer names the received amount only when the currencies differ. */
export const transferNeedsReceived = (from?: string, to?: string): boolean =>
  !!from && !!to && from !== to;

/** The size a photo is drawn at so its long side is at most `max`. */
export function targetSize(
  width: number,
  height: number,
  max: number,
): { width: number; height: number } {
  const long = Math.max(width, height);
  if (long <= max) return { width, height };
  const k = max / long;
  return { width: Math.round(width * k), height: Math.round(height * k) };
}

/** Grade of a budget: under 80 % ok, under 100 % near, else over. */
export function budgetState(
  spent: number,
  budget: number | null | undefined,
): 'ok' | 'near' | 'over' | null {
  if (!budget) return null;
  const share = spent / budget;
  return share < 0.8 ? 'ok' : share < 1 ? 'near' : 'over';
}

/** A phone photo scaled to 1600 px on the long side and re-encoded as JPEG. */
export async function scaleImage(
  file: File,
  max = 1600,
  quality = 0.85,
): Promise<{ dataUrl: string; bytes: number }> {
  const bitmap = await createImageBitmap(file);
  const size = targetSize(bitmap.width, bitmap.height, max);
  const canvas = document.createElement('canvas');
  canvas.width = size.width;
  canvas.height = size.height;
  canvas.getContext('2d')?.drawImage(bitmap, 0, 0, size.width, size.height);
  bitmap.close();
  const dataUrl = canvas.toDataURL('image/jpeg', quality);
  const base64 = dataUrl.slice(dataUrl.indexOf(',') + 1);
  return { dataUrl, bytes: Math.floor((base64.length * 3) / 4) };
}

/** The maximum the server takes, decoded. */
export const RECEIPT_MAX_BYTES = 4 * 1024 * 1024;

/** A calendar day as YYYY-MM-DD, for the forms' date fields. */
export const isDay = (date: string): boolean => parse(date) !== null;
