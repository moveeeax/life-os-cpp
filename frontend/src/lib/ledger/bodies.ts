/** The POST bodies of the add form, as the form built them inline (pinned by bodies.test.ts). */
import { isDay } from '@/lib/dates';

export interface ManualInput {
  type: 'expense' | 'income';
  date: string;
  account: string;
  amount: number;
  merchant: string;
  name: string;
  category: string;
  receiptAmount: string;
  receiptCurrency: string;
  ra: number | null;
  note: string;
}

export function manualBody(i: ManualInput) {
  return {
    type: i.type,
    date: i.date,
    account_id: i.account,
    amount: i.amount,
    merchant: i.merchant.trim(),
    name: (i.name.trim() || i.merchant.trim()).slice(0, 200),
    category_id: i.category,
    receipt_amount: i.receiptAmount.trim() ? i.ra : null,
    receipt_currency: i.receiptCurrency.trim() || null,
    note: i.note,
    source: 'manual' as const,
    status: 'posted' as const,
  };
}

export interface TransferInput {
  date: string;
  from: string;
  to: string;
  sent: number;
  received: number | null;
  needsReceived: boolean;
  fee: string;
  f: number | null;
  fromName: string;
  toName: string;
  note: string;
}

export function transferBody(i: TransferInput) {
  return {
    date: i.date,
    from_account_id: i.from,
    to_account_id: i.to,
    amount_sent: i.sent,
    amount_received: i.needsReceived ? i.received : null,
    fee: i.fee.trim() === '' ? null : i.f,
    name: `${i.fromName} → ${i.toName}`.slice(0, 200),
    note: i.note,
  };
}

export interface TransferAmounts {
  date: string;
  sent: number | null;
  received: number | null;
  needsReceived: boolean;
  fee: string;
  f: number | null;
}

/**
 * The first field that keeps a transfer from being sent, or null when it can
 * go: the add form's transfer tab and the transfer editor share this rule.
 */
export function transferProblem(i: TransferAmounts): 'sent' | 'received' | 'fee' | 'date' | null {
  if (i.sent === null || !(i.sent > 0)) return 'sent';
  if (i.needsReceived && (i.received === null || !(i.received > 0))) return 'received';
  if (i.fee.trim() !== '' && (i.f === null || !(i.f >= 0))) return 'fee';
  if (!isDay(i.date)) return 'date';
  return null;
}
