import { useState, type FormEvent } from 'react';
import { Plus } from 'lucide-react';
import { Link } from 'react-router';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { Modal } from '@/components/Modal';
import { useBalances, useCurrencies, useDeleteAccount, useSaveAccount } from '@/hooks/useMoney';
import { formatMoney, type Account, type AccountInput } from '@/lib/ledger';
import { decimalsOf } from '@/lib/ledger';
import { cn } from '@/lib/utils';

import {
  cardClass,
  dangerButton,
  inputClass,
  labelClass,
  modalPanel,
  primaryButton,
  secondaryButton,
} from '../workout/styles';
import { LoadError, MoneyFrame, Placeholder } from './frame';

const KINDS = [
  { value: 'card', label: 'Card' },
  { value: 'cash', label: 'Cash' },
  { value: 'deposit', label: 'Deposit' },
  { value: 'other', label: 'Other' },
] as const;

function AccountForm({ account, onClose }: { account: Account | null; onClose: () => void }) {
  const currencies = useCurrencies().data ?? [];
  const [name, setName] = useState(account?.name ?? '');
  const [bank, setBank] = useState(account?.bank ?? '');
  const [kind, setKind] = useState<AccountInput['kind']>(account?.kind ?? 'card');
  const [currency, setCurrency] = useState(account?.currency ?? 'KZT');
  const [last4, setLast4] = useState(account?.last4 ?? '');
  const [opening, setOpening] = useState(String(account?.opening_balance ?? 0));
  const [confirm, setConfirm] = useState(false);
  const [outcome, setOutcome] = useState<string | null>(null);
  const save = useSaveAccount(account?.id ?? null, onClose);
  const remove = useDeleteAccount((o) =>
    setOutcome(
      o === 'deleted'
        ? 'Deleted.'
        : 'Archived: rows refer to it, so it is hidden instead of removed.',
    ),
  );
  const openingValue = Number(opening.replace(',', '.'));
  const ok = name.trim() !== '' && /^[0-9]{0,4}$/.test(last4) && Number.isFinite(openingValue);

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (!ok) return;
    const body = {
      name: name.trim(),
      bank: bank.trim(),
      kind,
      last4,
      opening_balance: openingValue,
    };
    save.mutate(account ? body : { ...body, currency, position: 0 });
  };

  if (outcome) {
    return (
      <div className={modalPanel}>
        <p className="text-theme-sm">{outcome}</p>
        <div className="mt-5 flex justify-end">
          <button type="button" onClick={onClose} className={primaryButton}>
            Close
          </button>
        </div>
      </div>
    );
  }

  return (
    <>
      <form onSubmit={submit} className={modalPanel} aria-labelledby="account-form-title">
        <h2 id="account-form-title" className="text-lg font-semibold">
          {account ? 'Edit account' : 'New account'}
        </h2>
        <p className="mt-1 text-theme-xs text-gray-500">
          Name it the way the bank shows it: bank, product, currency, the last four digits.
        </p>
        <div className="mt-4 grid grid-cols-1 gap-4 sm:grid-cols-2">
          <div className="sm:col-span-2">
            <label htmlFor="acc-name" className={labelClass}>
              Name
            </label>
            <input
              id="acc-name"
              value={name}
              onChange={(e) => setName(e.target.value)}
              maxLength={120}
              placeholder="Kaspi Gold Visa •8880"
              className={inputClass}
            />
          </div>
          <div>
            <label htmlFor="acc-bank" className={labelClass}>
              Bank
            </label>
            <input
              id="acc-bank"
              value={bank}
              onChange={(e) => setBank(e.target.value)}
              maxLength={60}
              className={inputClass}
            />
          </div>
          <div>
            <label htmlFor="acc-kind" className={labelClass}>
              Kind
            </label>
            <select
              id="acc-kind"
              value={kind}
              onChange={(e) => setKind(e.target.value as AccountInput['kind'])}
              className={inputClass}
            >
              {KINDS.map((k) => (
                <option key={k.value} value={k.value}>
                  {k.label}
                </option>
              ))}
            </select>
          </div>
          <div>
            <label htmlFor="acc-currency" className={labelClass}>
              Currency{account && ' (fixed: the rows are in it)'}
            </label>
            <select
              id="acc-currency"
              value={currency}
              onChange={(e) => setCurrency(e.target.value)}
              disabled={!!account}
              className={inputClass}
            >
              {currencies.map((c) => (
                <option key={c.code} value={c.code}>
                  {c.code} · {c.name}
                </option>
              ))}
            </select>
          </div>
          <div>
            <label htmlFor="acc-last4" className={labelClass}>
              Last four digits
            </label>
            <input
              id="acc-last4"
              inputMode="numeric"
              value={last4}
              onChange={(e) => setLast4(e.target.value.replace(/\D/g, '').slice(0, 4))}
              className={inputClass}
            />
          </div>
          <div>
            <label htmlFor="acc-opening" className={labelClass}>
              Opening balance
            </label>
            <input
              id="acc-opening"
              inputMode="decimal"
              value={opening}
              onChange={(e) => setOpening(e.target.value)}
              className={inputClass}
            />
          </div>
        </div>
        {(save.error || remove.error) && (
          <p role="alert" className="mt-3 text-theme-sm text-error-500">
            {save.error ?? remove.error}
          </p>
        )}
        <div className="mt-5 flex flex-wrap justify-between gap-3">
          {account ? (
            <button type="button" onClick={() => setConfirm(true)} className={dangerButton}>
              Delete
            </button>
          ) : (
            <span />
          )}
          <div className="flex gap-3">
            <button type="button" onClick={onClose} className={secondaryButton}>
              Cancel
            </button>
            <button type="submit" disabled={save.isPending || !ok} className={primaryButton}>
              {save.isPending ? 'Saving…' : account ? 'Save' : 'Create'}
            </button>
          </div>
        </div>
      </form>
      {confirm && account && (
        <ConfirmDialog
          title="Delete this account?"
          description="An account with rows is archived instead."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => {
            setConfirm(false);
            remove.mutate(account.id);
          }}
          onClose={() => setConfirm(false)}
        />
      )}
    </>
  );
}

/** Accounts grouped by currency, with balances computed from the ledger. */
export function MoneyAccountsPage() {
  const balances = useBalances();
  const currencies = useCurrencies().data;
  const [open, setOpen] = useState<Account | 'new' | null>(null);

  return (
    <MoneyFrame
      title="Accounts"
      actions={
        <button type="button" onClick={() => setOpen('new')} className={primaryButton}>
          <Plus className="size-4" aria-hidden="true" />
          New account
        </button>
      }
    >
      {balances.isPending ? (
        <Placeholder className="h-48" />
      ) : balances.isError ? (
        <LoadError error={balances.error} onRetry={() => void balances.refetch()} />
      ) : balances.data.length === 0 ? (
        <p className={cn(cardClass, 'text-center text-theme-sm text-gray-500')}>No accounts yet.</p>
      ) : (
        balances.data.map((g) => (
          <section key={g.currency} aria-label={g.currency}>
            <div className="flex items-baseline justify-between gap-3">
              <h2 className="text-base font-semibold text-gray-800 dark:text-white/90">
                {g.currency}
              </h2>
              <p className="text-theme-sm font-medium text-gray-800 tabular-nums dark:text-white/90">
                {formatMoney(g.total, g.currency, decimalsOf(currencies, g.currency))}
              </p>
            </div>
            <div className="mt-2 grid gap-3 sm:grid-cols-2 xl:grid-cols-3">
              {g.accounts.map((a) => (
                <div key={a.id} className={cardClass}>
                  <div className="flex items-start justify-between gap-3">
                    <div className="min-w-0">
                      <p className="truncate text-theme-sm font-medium text-gray-800 dark:text-white/90">
                        {a.name}
                      </p>
                      <p className="text-theme-xs text-gray-500">
                        {[a.bank, a.kind, a.last4 && `•${a.last4}`].filter(Boolean).join(' · ')}
                      </p>
                    </div>
                    <button
                      type="button"
                      onClick={() => setOpen(a)}
                      className={cn(secondaryButton, 'px-3 py-1.5')}
                    >
                      Edit
                    </button>
                  </div>
                  <p className="mt-3 text-xl font-semibold text-gray-800 tabular-nums dark:text-white/90">
                    {formatMoney(a.balance, a.currency, decimalsOf(currencies, a.currency))}
                  </p>
                  <p className="mt-1 flex justify-between text-theme-xs text-gray-500">
                    <span>
                      {a.last_activity ? `Last activity ${a.last_activity}` : 'No activity yet'}
                    </span>
                    <Link to={`/money?account=${a.id}`} className="text-brand-500 hover:underline">
                      Rows
                    </Link>
                  </p>
                </div>
              ))}
            </div>
          </section>
        ))
      )}
      {open && (
        <Modal onClose={() => setOpen(null)} className="max-w-xl">
          <AccountForm account={open === 'new' ? null : open} onClose={() => setOpen(null)} />
        </Modal>
      )}
    </MoneyFrame>
  );
}
