import { useState, type FormEvent } from 'react';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { useDeleteTransaction, useUpdateTransaction } from '@/hooks/useMoney';
import {
  isDay,
  type Account,
  type Category,
  type Transaction,
  type TransactionPatch,
} from '@/lib/ledger';
import { parseAmount } from '@/lib/amounts';

import {
  dangerButton,
  inputClass,
  labelClass,
  modalPanel,
  primaryButton,
  secondaryButton,
} from '@/shell/kit/styles';
import { CategorySelect } from './bits';

/** The ledger row editor: amount, day, name, merchant, category and note; delete asks first. */
export function TransactionEditor({
  row,
  accounts,
  categories,
  onClose,
}: {
  row: Transaction;
  accounts: Account[];
  categories: Category[];
  onClose: () => void;
}) {
  const adjustment = row.type === 'fx_adjustment';
  const [amount, setAmount] = useState(String(row.amount));
  const [name, setName] = useState(row.name);
  const [merchant, setMerchant] = useState(row.merchant);
  const [category, setCategory] = useState(row.category_id ?? '');
  const [date, setDate] = useState(row.date);
  const [note, setNote] = useState(row.note);
  const [confirm, setConfirm] = useState(false);
  const update = useUpdateTransaction(row.id, onClose);
  const remove = useDeleteTransaction(onClose);
  // A bank recalculation may be negative; an income or an expense may not.
  const a = parseAmount(amount, { signed: adjustment });
  const amountBad =
    a === null ||
    (adjustment ? a === 0 : a <= 0) ||
    (!adjustment && (name.trim() === '' || !isDay(date)));

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (amountBad || a === null) return;
    const patch: TransactionPatch = {};
    if (a !== row.amount) patch.amount = a;
    if (note !== row.note) patch.note = note;
    if (!adjustment) {
      if (name !== row.name) patch.name = name;
      if (merchant !== row.merchant) patch.merchant = merchant;
      if (category && category !== row.category_id) patch.category_id = category;
      if (date !== row.date) patch.date = date;
    }
    if (Object.keys(patch).length === 0) {
      onClose();
      return;
    }
    update.mutate(patch);
  };

  const account = accounts.find((x) => x.id === row.account_id);
  return (
    <>
      <form onSubmit={submit} className={modalPanel} aria-labelledby="tx-editor-title">
        <h2 id="tx-editor-title" className="truncate text-lg font-semibold">
          {adjustment ? 'Bank recalculation' : row.name}
        </h2>
        <p className="mt-1 text-theme-sm text-gray-500">
          {account?.name} · {row.currency}
          {row.receipt_amount !== null && (
            <>
              {' · receipt '}
              <span className="num">
                {row.receipt_amount} {row.receipt_currency}
              </span>
            </>
          )}
          {row.fx_note && (
            <>
              {' · '}
              <span className="num">{row.fx_note}</span>
            </>
          )}
        </p>
        <div className="mt-4 grid grid-cols-1 gap-4 sm:grid-cols-2">
          <div>
            <label htmlFor="e-amount" className={labelClass}>
              Amount, {row.currency}
              {adjustment && ' (plus when the bank took more)'}
            </label>
            <input
              id="e-amount"
              inputMode="decimal"
              value={amount}
              onChange={(e) => setAmount(e.target.value)}
              aria-invalid={amountBad || undefined}
              className={inputClass}
            />
          </div>
          {!adjustment && (
            <>
              <div>
                <label htmlFor="e-date" className={labelClass}>
                  Date
                </label>
                <input
                  id="e-date"
                  type="date"
                  value={date}
                  onChange={(e) => setDate(e.target.value)}
                  className={inputClass}
                />
              </div>
              <div className="sm:col-span-2">
                <label htmlFor="e-name" className={labelClass}>
                  Name
                </label>
                <input
                  id="e-name"
                  value={name}
                  onChange={(e) => setName(e.target.value)}
                  maxLength={200}
                  className={inputClass}
                />
              </div>
              <div>
                <label htmlFor="e-merchant" className={labelClass}>
                  Merchant
                </label>
                <input
                  id="e-merchant"
                  value={merchant}
                  onChange={(e) => setMerchant(e.target.value)}
                  maxLength={200}
                  className={inputClass}
                />
              </div>
              <CategorySelect
                id="e-category"
                value={category}
                onChange={setCategory}
                categories={categories}
                // Not an adjustment here, so the row is an income or an expense.
                kind={row.type as 'income' | 'expense'}
                placeholder={null}
              />
            </>
          )}
          <div className="sm:col-span-2">
            <label htmlFor="e-note" className={labelClass}>
              Note
            </label>
            <input
              id="e-note"
              value={note}
              onChange={(e) => setNote(e.target.value)}
              maxLength={2000}
              className={inputClass}
            />
          </div>
        </div>
        {(update.error || remove.error) && (
          <p role="alert" className="mt-3 text-theme-sm text-error-500">
            {update.error ?? remove.error}
          </p>
        )}
        <div className="mt-5 flex flex-wrap justify-between gap-3">
          <button type="button" onClick={() => setConfirm(true)} className={dangerButton}>
            Delete
          </button>
          <div className="flex gap-3">
            <button type="button" onClick={onClose} className={secondaryButton}>
              Cancel
            </button>
            <button
              type="submit"
              disabled={update.isPending || amountBad}
              className={primaryButton}
            >
              {update.isPending ? 'Saving…' : 'Save'}
            </button>
          </div>
        </div>
      </form>
      {confirm && (
        <ConfirmDialog
          title="Delete this row?"
          description={
            row.adjustments.length > 0 ? 'Its bank recalculations go with it.' : undefined
          }
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate(row.id)}
          onClose={() => setConfirm(false)}
        />
      )}
    </>
  );
}
