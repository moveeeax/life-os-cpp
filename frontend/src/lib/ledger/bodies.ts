/** The POST bodies of the add form, as the form built them inline (pinned by bodies.test.ts). */

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
