import { formatAmount, parseAmount } from '@/lib/amounts';
import { formatDay } from '@/lib/dates';

import type {
  PracticeAmount,
  PracticeDeal,
  PracticeDealInput,
  PracticeDealStage,
  PracticeEntry,
  PracticeEntryInput,
} from './types';

export type * from './types';
export { todayLocal } from '@/lib/dates';

/** The stages in the order the deal list shows them: open ones first. */
export const STAGES: readonly PracticeDealStage[] = [
  'lead',
  'negotiation',
  'active',
  'done',
  'lost',
] as const;

export const OPEN_STAGES: readonly PracticeDealStage[] = ['lead', 'negotiation', 'active'];

export const STAGE_LABEL: Record<PracticeDealStage, string> = {
  lead: 'Lead',
  negotiation: 'Negotiation',
  active: 'Active',
  done: 'Done',
  lost: 'Lost',
};

/** The group label of a stage over its deals. */
export const STAGE_GROUP: Record<PracticeDealStage, string> = {
  lead: 'Leads',
  negotiation: 'Negotiation',
  active: 'Active',
  done: 'Done',
  lost: 'Lost',
};

/** The decimals of a currency as Intl knows them (JPY 0, KWD 3); 2 for a code it does not know. */
export function currencyDigits(code: string): number {
  try {
    return (
      new Intl.NumberFormat('en-US', { style: 'currency', currency: code }).resolvedOptions()
        .maximumFractionDigits ?? 2
    );
  } catch {
    return 2;
  }
}

/** "1,500 USD", "1,500.50 USD": an amount in its own currency, cents only when there are any. */
export function money(amount: number, currency: string, signed = false): string {
  return formatAmount(amount, {
    currency,
    digits: currencyDigits(currency),
    wholeWhenWhole: true,
    signed,
  });
}

/** Several currencies side by side, never added: "1,000 USD · 250,000 KZT". */
export function amountsLine(amounts: readonly PracticeAmount[]): string {
  return amounts.map((a) => money(a.amount, a.currency)).join(' · ');
}

/** The net of each currency, signed: "+980 USD · +250,000 KZT · −12.50 EUR". */
export function netLine(totals: readonly { currency: string; net: number }[]): string {
  return totals.map((t) => money(t.net, t.currency, true)).join(' · ');
}

/** "2026-10" → "October 2026". */
export function monthLabel(month: string): string {
  const [y, m] = month.split('-').map(Number);
  return new Date(Date.UTC(y, m - 1, 1)).toLocaleDateString('en-GB', {
    month: 'long',
    year: 'numeric',
    timeZone: 'UTC',
  });
}

/** The month of a day: "2026-10-19" → "2026-10". */
export function monthOf(day: string): string {
  return day.slice(0, 7);
}

/** "Oct 3" for a day of this year, "Oct 3, 2025" otherwise. */
export function shortDay(iso: string, today: string): string {
  return formatDay(iso, { yearUnless: today });
}

/** The row title of an entry: its note, else its deal, else its client, else its kind. */
export function entryTitle(e: PracticeEntry): string {
  return e.note || e.deal_title || e.client_name || (e.kind === 'income' ? 'Income' : 'Expense');
}

/** The deals of each stage, in STAGES order; a stage without deals is left out. */
export function dealsByStage(
  deals: readonly PracticeDeal[],
): { stage: PracticeDealStage; deals: PracticeDeal[] }[] {
  return STAGES.map((stage) => ({ stage, deals: deals.filter((d) => d.stage === stage) })).filter(
    (g) => g.deals.length > 0,
  );
}

/** "Oct 1 – Nov 30", "from Oct 1", "until Nov 30", or "" without dates. */
export function dealDates(d: Pick<PracticeDeal, 'start_date' | 'end_date'>, today: string): string {
  if (d.start_date && d.end_date)
    return `${shortDay(d.start_date, today)} – ${shortDay(d.end_date, today)}`;
  if (d.start_date) return `from ${shortDay(d.start_date, today)}`;
  if (d.end_date) return `until ${shortDay(d.end_date, today)}`;
  return '';
}

/** The entries of a list by month, the latest first (the list comes sorted by day). */
export function byMonth(
  entries: readonly PracticeEntry[],
): { month: string; rows: PracticeEntry[] }[] {
  const out: { month: string; rows: PracticeEntry[] }[] = [];
  for (const e of entries) {
    const m = monthOf(e.date);
    if (out.length === 0 || out[out.length - 1].month !== m) out.push({ month: m, rows: [] });
    out[out.length - 1].rows.push(e);
  }
  return out;
}

// ── form bodies ────────────────────────────────────────────────────────────

export interface DealForm {
  client_id: string;
  title: string;
  stage: PracticeDealStage;
  value: string;
  currency: string;
  start_date: string;
  end_date: string;
  note: string;
}

/** The body of a deal form, or the reason it cannot be sent. */
export function dealBody(f: DealForm): { body: PracticeDealInput } | { error: string } {
  const value = f.value.trim() === '' ? null : parseAmount(f.value);
  const currency = f.currency.trim().toUpperCase();
  if (f.value.trim() !== '' && (value === null || value <= 0))
    return { error: 'The value is a positive number, like 1500 or 1500.50.' };
  if (value !== null && !/^[A-Z]{3}$/.test(currency))
    return { error: 'A value needs its currency, a code like USD.' };
  if (f.start_date && f.end_date && f.end_date < f.start_date)
    return { error: 'The end is on or after the start.' };
  return {
    body: {
      client_id: f.client_id,
      title: f.title.trim(),
      stage: f.stage,
      value,
      currency: value === null ? null : currency,
      start_date: f.start_date || null,
      end_date: f.end_date || null,
      note: f.note.trim(),
    },
  };
}

export interface EntryForm {
  kind: 'income' | 'expense';
  date: string;
  amount: string;
  currency: string;
  client_id: string;
  deal_id: string;
  note: string;
}

/** The body of an income or expense form, or the reason it cannot be sent. */
export function entryBody(f: EntryForm): { body: PracticeEntryInput } | { error: string } {
  const amount = parseAmount(f.amount);
  const currency = f.currency.trim().toUpperCase();
  if (amount === null || amount <= 0)
    return { error: 'The amount is a positive number, like 1500 or 1500.50.' };
  if (!/^[A-Z]{3}$/.test(currency)) return { error: 'The currency is a code like USD.' };
  if (!f.date) return { error: 'Pick the day.' };
  return {
    body: {
      kind: f.kind,
      date: f.date,
      amount,
      currency,
      // A deal brings its own client; the server fills it.
      client_id: f.deal_id ? null : f.client_id || null,
      deal_id: f.deal_id || null,
      note: f.note.trim(),
    },
  };
}
