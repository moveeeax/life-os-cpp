import { useState, type FormEvent } from 'react';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { Modal } from '@/components/Modal';
import { useDeleteTransfer, useTransfers, useUpdateTransfer } from '@/hooks/useMoney';
import {
  decimalsOf,
  formatMoney,
  isDay,
  type Account,
  type Currency,
  type Transfer,
} from '@/lib/ledger';

import {
  cardClass,
  dangerButton,
  inputClass,
  labelClass,
  modalPanel,
  primaryButton,
  secondaryButton,
} from '../workout/styles';

const num = (v: string): number | null => {
  const t = v.trim().replace(',', '.');
  if (t === '') return null;
  const n = Number(t);
  return Number.isFinite(n) ? n : null;
};

function TransferEditor({ transfer, onClose }: { transfer: Transfer; onClose: () => void }) {
  const cross = transfer.amount_received !== null;
  const [date, setDate] = useState(transfer.date);
  const [sent, setSent] = useState(String(transfer.amount_sent));
  const [received, setReceived] = useState(
    transfer.amount_received === null ? '' : String(transfer.amount_received),
  );
  const [fee, setFee] = useState(transfer.fee === null ? '' : String(transfer.fee));
  const [note, setNote] = useState(transfer.note);
  const [confirm, setConfirm] = useState(false);
  const update = useUpdateTransfer(transfer.id, onClose);
  const remove = useDeleteTransfer(onClose);
  const s = num(sent);
  const r = num(received);
  const f = num(fee);
  const ok =
    isDay(date) &&
    s !== null &&
    s > 0 &&
    (!cross || (r !== null && r > 0)) &&
    (fee.trim() === '' || (f !== null && f >= 0));

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (!ok) return;
    update.mutate({
      date,
      amount_sent: s!,
      ...(cross ? { amount_received: r } : {}),
      fee: fee.trim() === '' ? null : f,
      note,
    });
  };

  return (
    <>
      <form onSubmit={submit} className={modalPanel} aria-labelledby="transfer-editor-title">
        <h2 id="transfer-editor-title" className="truncate text-lg font-semibold">
          {transfer.name || 'Transfer'}
        </h2>
        <div className="mt-4 grid grid-cols-1 gap-4 sm:grid-cols-2">
          <div>
            <label htmlFor="te-date" className={labelClass}>
              Date
            </label>
            <input
              id="te-date"
              type="date"
              value={date}
              onChange={(e) => setDate(e.target.value)}
              className={inputClass}
            />
          </div>
          <div>
            <label htmlFor="te-sent" className={labelClass}>
              Sent, {transfer.from_currency}
            </label>
            <input
              id="te-sent"
              inputMode="decimal"
              value={sent}
              onChange={(e) => setSent(e.target.value)}
              className={inputClass}
            />
          </div>
          {cross && (
            <div>
              <label htmlFor="te-received" className={labelClass}>
                Received, {transfer.to_currency}
              </label>
              <input
                id="te-received"
                inputMode="decimal"
                value={received}
                onChange={(e) => setReceived(e.target.value)}
                className={inputClass}
              />
            </div>
          )}
          <div>
            <label htmlFor="te-fee" className={labelClass}>
              Fee, {transfer.from_currency}
            </label>
            <input
              id="te-fee"
              inputMode="decimal"
              value={fee}
              onChange={(e) => setFee(e.target.value)}
              className={inputClass}
            />
          </div>
          <div className="sm:col-span-2">
            <label htmlFor="te-note" className={labelClass}>
              Note
            </label>
            <input
              id="te-note"
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
            <button type="submit" disabled={update.isPending || !ok} className={primaryButton}>
              {update.isPending ? 'Saving…' : 'Save'}
            </button>
          </div>
        </div>
      </form>
      {confirm && (
        <ConfirmDialog
          title="Delete this transfer?"
          description="Both balances move back."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate(transfer.id)}
          onClose={() => setConfirm(false)}
        />
      )}
    </>
  );
}

/** The period's transfers and exchanges, each open for editing. */
export function TransfersList({
  from,
  to,
  account,
  accounts,
  currencies,
}: {
  from: string;
  to: string;
  account?: string;
  accounts: Account[];
  currencies?: Currency[];
}) {
  const transfers = useTransfers(from, to, account || undefined);
  const [open, setOpen] = useState<Transfer | null>(null);
  const rows = transfers.data ?? [];
  if (rows.length === 0) return null;
  const name = (id: string) => accounts.find((a) => a.id === id)?.name ?? '';
  const money = (n: number, c: string) => formatMoney(n, c, decimalsOf(currencies, c));
  return (
    <section className={cardClass} aria-label="Transfers">
      <h2 className="text-theme-sm font-semibold text-gray-800 dark:text-white/90">
        Transfers and exchanges
      </h2>
      <ul className="mt-2 divide-y divide-gray-100 dark:divide-white/5">
        {rows.map((t) => (
          <li key={t.id}>
            <button
              type="button"
              onClick={() => setOpen(t)}
              className="flex w-full items-center gap-3 py-2.5 text-start hover:bg-gray-50 dark:hover:bg-white/5"
            >
              <span className="min-w-0 flex-1">
                <span className="block truncate text-theme-sm text-gray-800 dark:text-white/90">
                  {name(t.from_account_id)} → {name(t.to_account_id)}
                </span>
                <span className="block truncate text-theme-xs text-gray-500">
                  {t.date}
                  {t.cost_rate !== null &&
                    ` · ${t.cost_rate.toFixed(4)} ${t.from_currency} per ${t.to_currency}`}
                  {t.fee !== null && t.fee > 0 && ` · fee ${money(t.fee, t.from_currency)}`}
                </span>
              </span>
              <span className="text-end text-theme-sm tabular-nums">
                <span className="block">−{money(t.amount_sent, t.from_currency)}</span>
                {t.amount_received !== null && (
                  <span className="block text-theme-xs text-success-600 dark:text-success-500">
                    +{money(t.amount_received, t.to_currency)}
                  </span>
                )}
              </span>
            </button>
          </li>
        ))}
      </ul>
      {open && (
        <Modal onClose={() => setOpen(null)} className="max-w-lg">
          <TransferEditor transfer={open} onClose={() => setOpen(null)} />
        </Modal>
      )}
    </section>
  );
}
