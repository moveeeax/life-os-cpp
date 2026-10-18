import { useState, type FormEvent } from 'react';
import { Link } from 'react-router';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { Modal } from '@/components/Modal';
import {
  useAccounts,
  useBalances,
  useCurrencies,
  useDeleteAccount,
  useMoneySummary,
  useSaveAccount,
} from '@/hooks/useMoney';
import { formatMoney, type Account, type AccountInput, type Currency } from '@/lib/ledger';
import { decimalsOf } from '@/lib/ledger';
import { SMALL_LIMITS, valueAccounts, viewAccounts, type Valued } from '@/lib/ledger/accountsView';
import { parseAmount } from '@/lib/amounts';

import {
  dangerButton,
  inputClass,
  labelClass,
  modalPanel,
  primaryButton,
  secondaryButton,
} from '@/shell/kit/styles';
import { Group, Num, Row } from '@/shell/kit';

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
  const [archived, setArchived] = useState(account?.archived ?? false);
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
  // An opening balance may be negative (a card in debt on the day it was added).
  const openingValue = parseAmount(opening, { signed: true });
  const ok = name.trim() !== '' && /^[0-9]{0,4}$/.test(last4) && openingValue !== null;

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (!ok || openingValue === null) return;
    const body = {
      name: name.trim(),
      bank: bank.trim(),
      kind,
      last4,
      opening_balance: openingValue,
    };
    save.mutate(account ? { ...body, archived } : { ...body, currency, position: 0 });
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
              placeholder="Everyday card"
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
          {account && (
            <label className="flex items-center gap-2 self-end pb-2 text-theme-sm">
              <input
                type="checkbox"
                checked={archived}
                onChange={(e) => setArchived(e.target.checked)}
              />
              Archived (hidden from pickers and totals)
            </label>
          )}
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

type View = { by: 'value' | 'currency'; small: number; archived: boolean };
const VIEW_KEY = 'money:accounts:view';
const DEFAULT_VIEW: View = { by: 'value', small: 0, archived: false };

/** The remembered way of looking at the list; a per-browser convenience. */
function readView(): View {
  try {
    const v = JSON.parse(globalThis.localStorage?.getItem(VIEW_KEY) ?? 'null') as Partial<View>;
    return {
      by: v?.by === 'currency' ? 'currency' : 'value',
      small: SMALL_LIMITS.includes(v?.small as (typeof SMALL_LIMITS)[number]) ? v!.small! : 0,
      archived: v?.archived === true,
    };
  } catch {
    return DEFAULT_VIEW;
  }
}

function saveView(v: View) {
  try {
    globalThis.localStorage?.setItem(VIEW_KEY, JSON.stringify(v));
  } catch {
    /* private window: the choice lasts until reload */
  }
}

const usd = (v: number) => formatMoney(v, 'USD', Math.abs(v) >= 100 ? 0 : 2);

function AccountRow({
  item,
  currencies,
  onEdit,
}: {
  item: Valued<Account>;
  currencies: Currency[] | undefined;
  onEdit: (a: Account) => void;
}) {
  const a = item.account;
  const facts = [a.bank, a.kind, a.last4 && `•${a.last4}`].filter(Boolean).join(' · ');
  return (
    <Row
      title={a.name}
      sub={
        <>
          {facts}
          {a.currency !== 'USD' && item.value !== null && (
            <>
              {facts && ' · '}≈ <Num>{usd(item.value)}</Num>
            </>
          )}
        </>
      }
      right={<Num>{formatMoney(a.balance, a.currency, decimalsOf(currencies, a.currency))}</Num>}
    >
      <span>{a.last_activity ? `Last activity ${a.last_activity}` : 'No activity yet'}</span>
      <span className="k-actions">
        <Link to={`/money?account=${a.id}`} className="k-link">
          Rows
        </Link>
        <button type="button" className="k-link" onClick={() => onEdit(a)}>
          Edit
        </button>
      </span>
    </Row>
  );
}

function HiddenNote({ count, limit }: { count: number; limit: number }) {
  if (count === 0) return null;
  return (
    <p className="k-empty" data-num-ok>
      {count} {count === 1 ? 'account' : 'accounts'} under {usd(limit)} hidden
    </p>
  );
}

/**
 * The accounts with their balances from the ledger: by dollar value, largest
 * first, or grouped by currency; small ones (by dollar value) and archived
 * ones can be kept out of sight.
 */
export function MoneyAccountsPage() {
  const balances = useBalances();
  const summary = useMoneySummary('USD', 30);
  const all = useAccounts(true);
  const currencies = useCurrencies().data;
  const [open, setOpen] = useState<Account | 'new' | null>(null);
  const [view, setViewState] = useState<View>(readView);
  const setView = (patch: Partial<View>) => {
    const next = { ...view, ...patch };
    setViewState(next);
    saveView(next);
  };

  const rows = summary.data?.accounts ?? [];
  const active = valueAccounts(balances.data?.flatMap((g) => g.accounts) ?? [], rows);
  const archived = valueAccounts(
    (all.data ?? []).filter((a) => a.archived),
    rows,
  );
  const archivedView = viewAccounts(archived, view.small);
  const edit = (a: Account) => setOpen(a);

  return (
    <MoneyFrame
      title="Accounts"
      actions={
        <>
          <div className="k-seg" role="group" aria-label="Order">
            {(
              [
                ['value', 'By value'],
                ['currency', 'By currency'],
              ] as const
            ).map(([key, label]) => (
              <button
                key={key}
                type="button"
                aria-pressed={view.by === key}
                className={view.by === key ? 'k-seg-on' : undefined}
                onClick={() => setView({ by: key })}
              >
                {label}
              </button>
            ))}
          </div>
          <label className="k-check" data-num-ok>
            Hide under
            <select
              className="k-input k-select-inline"
              aria-label="Hide accounts worth less than"
              value={view.small}
              onChange={(e) => setView({ small: Number(e.target.value) })}
            >
              {SMALL_LIMITS.map((l) => (
                <option key={l} value={l}>
                  {l === 0 ? 'nothing' : usd(l)}
                </option>
              ))}
            </select>
          </label>
          <label className="k-check">
            <input
              type="checkbox"
              checked={view.archived}
              onChange={(e) => setView({ archived: e.target.checked })}
            />
            Archived
          </label>
          <span className="k-grow" />
          <button type="button" onClick={() => setOpen('new')} className={primaryButton}>
            New account
          </button>
        </>
      }
    >
      {balances.isPending ? (
        <Placeholder className="h-48" />
      ) : balances.isError ? (
        <LoadError error={balances.error} onRetry={() => void balances.refetch()} />
      ) : balances.data.length === 0 && archived.length === 0 ? (
        <p className="k-empty">No accounts yet.</p>
      ) : view.by === 'value' ? (
        (() => {
          const v = viewAccounts(active, view.small);
          return (
            <Group
              label="By value in USD"
              total={summary.data ? <Num>≈ {usd(summary.data.total)}</Num> : undefined}
            >
              {v.shown.map((item) => (
                <AccountRow
                  key={item.account.id}
                  item={item}
                  currencies={currencies}
                  onEdit={edit}
                />
              ))}
              <HiddenNote count={v.hidden} limit={view.small} />
            </Group>
          );
        })()
      ) : (
        balances.data.map((g) => {
          const v = viewAccounts(
            active.filter((x) => x.account.currency === g.currency),
            view.small,
          );
          if (v.shown.length === 0 && v.hidden > 0) return null;
          return (
            <Group
              key={g.currency}
              label={g.currency}
              total={
                <Num>{formatMoney(g.total, g.currency, decimalsOf(currencies, g.currency))}</Num>
              }
            >
              {v.shown.map((item) => (
                <AccountRow
                  key={item.account.id}
                  item={item}
                  currencies={currencies}
                  onEdit={edit}
                />
              ))}
              <HiddenNote count={v.hidden} limit={view.small} />
            </Group>
          );
        })
      )}
      {view.archived && archived.length > 0 && (
        <Group label="Archived">
          {archivedView.shown.map((item) => (
            <AccountRow key={item.account.id} item={item} currencies={currencies} onEdit={edit} />
          ))}
          <HiddenNote count={archivedView.hidden} limit={view.small} />
        </Group>
      )}
      {open && (
        <Modal onClose={() => setOpen(null)} className="max-w-xl">
          <AccountForm account={open === 'new' ? null : open} onClose={() => setOpen(null)} />
        </Modal>
      )}
    </MoneyFrame>
  );
}
