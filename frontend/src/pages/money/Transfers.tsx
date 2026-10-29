import { useState, type FormEvent } from 'react';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { Modal } from '@/components/Modal';
import { useDeleteTransfer, useUpdateTransfer } from '@/hooks/useMoney';
import {
  decimalsOf,
  formatDay,
  formatMoney,
  isDay,
  rateLine,
  type Account,
  type Currency,
  type Transfer,
} from '@/lib/ledger';
import { parseAmount } from '@/lib/amounts';

import { Num, Row } from '@/shell/kit';
import {
  dangerButton,
  inputClass,
  labelClass,
  modalPanel,
  primaryButton,
  secondaryButton,
} from '@/shell/kit/styles';

const num = parseAmount;

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

/** "Rate 520 KZT per USD": the cost rate turned over below 1, six significant digits. */
function RateText({ rate, from, to }: { rate: number; from: string; to: string }) {
  const line = rateLine(rate, from, to);
  if (!line) return null;
  return (
    <span>
      Rate <Num>{line.value}</Num> {line.unit}
    </span>
  );
}

/**
 * A transfer or an exchange as one line of the ledger: the accounts, the day and
 * what moved; the rate, the fee and the editor on click.
 */
export function TransferRow({
  transfer: t,
  accounts,
  currencies,
}: {
  transfer: Transfer;
  accounts: Account[];
  currencies?: Currency[];
}) {
  const [open, setOpen] = useState(false);
  const name = (id: string) => accounts.find((a) => a.id === id)?.name ?? '';
  const money = (n: number, c: string) => formatMoney(n, c, decimalsOf(currencies, c));
  return (
    <>
      <Row
        dot="transparent"
        title={t.name || `${name(t.from_account_id)} → ${name(t.to_account_id)}`}
        right={
          <span className="l-right">
            <span className="l-date" data-num-ok>
              {formatDay(t.date, { weekday: false })}
            </span>{' '}
            · <Num>{money(t.amount_sent, t.from_currency)}</Num>
            {t.amount_received !== null && (
              <>
                {' → '}
                <Num>{money(t.amount_received, t.to_currency)}</Num>
              </>
            )}
          </span>
        }
      >
        <span>
          {name(t.from_account_id)} → {name(t.to_account_id)}
        </span>
        {t.cost_rate !== null && (
          <RateText rate={t.cost_rate} from={t.from_currency} to={t.to_currency} />
        )}
        {t.fee !== null && t.fee > 0 && (
          <span>
            Fee <Num>{money(t.fee, t.from_currency)}</Num>
          </span>
        )}
        {t.note && <span>{t.note}</span>}
        <span className="k-actions">
          <button type="button" className="k-link" onClick={() => setOpen(true)}>
            Edit or delete
          </button>
        </span>
      </Row>
      {open && (
        <Modal onClose={() => setOpen(false)} className="max-w-lg">
          <TransferEditor transfer={t} onClose={() => setOpen(false)} />
        </Modal>
      )}
    </>
  );
}
