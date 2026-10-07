import { useEffect, useMemo, useState, type FormEvent } from 'react';
import { Plus } from 'lucide-react';
import { useSearchParams } from 'react-router';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { Modal } from '@/components/Modal';
import {
  useAccounts,
  useCategories,
  useConfirmTransaction,
  useCurrencies,
  useDeleteTransaction,
  useInbox,
  useReport,
  useTransactions,
  useUpdateTransaction,
} from '@/hooks/useMoney';
import {
  decimalsOf,
  formatDay,
  formatMoney,
  groupByDay,
  isDay,
  periodFromSearch,
  periodRange,
  todayLocal,
  totalsByCurrency,
  type Account,
  type Category,
  type InboxRow,
  type PeriodKind,
  type Transaction,
  type TransactionPatch,
} from '@/lib/ledger';
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
import { AddForm } from './AddForm';
import { Amount, CurrencyTotals, PeriodRow } from './bits';
import { LoadError, MoneyFrame, Placeholder } from './frame';
import { TransfersList } from './Transfers';

function TransactionEditor({
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
  const a = Number(amount.replace(',', '.'));
  const amountBad =
    !Number.isFinite(a) ||
    (adjustment ? a === 0 : a <= 0) ||
    (!adjustment && (name.trim() === '' || !isDay(date)));

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (amountBad) return;
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
          {row.receipt_amount !== null &&
            ` · receipt ${row.receipt_amount} ${row.receipt_currency}`}
          {row.fx_note && ` · ${row.fx_note}`}
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
              <div>
                <label htmlFor="e-category" className={labelClass}>
                  Category
                </label>
                <select
                  id="e-category"
                  value={category}
                  onChange={(e) => setCategory(e.target.value)}
                  className={inputClass}
                >
                  {categories
                    .filter((c) => c.kind === row.type)
                    .map((c) => (
                      <option key={c.id} value={c.id}>
                        {c.name}
                      </option>
                    ))}
                </select>
              </div>
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

function Row({
  row,
  account,
  category,
  onOpen,
  currencies,
}: {
  row: Transaction;
  account?: Account;
  category?: Category;
  onOpen: () => void;
  currencies: ReturnType<typeof useCurrencies>['data'];
}) {
  const adjustment = row.type === 'fx_adjustment';
  return (
    <li>
      <button
        type="button"
        onClick={onOpen}
        className={cn(
          'flex w-full items-center gap-3 py-2.5 text-start hover:bg-gray-50 dark:hover:bg-white/5',
          adjustment && 'ps-5',
        )}
      >
        <span className="min-w-0 flex-1">
          <span className="block truncate text-theme-sm text-gray-800 dark:text-white/90">
            {adjustment ? 'Bank recalculation' : row.name}
          </span>
          <span className="block truncate text-theme-xs text-gray-500 dark:text-gray-400">
            {[
              row.merchant && row.merchant !== row.name ? row.merchant : null,
              category?.name,
              account?.name,
            ]
              .filter(Boolean)
              .join(' · ')}
          </span>
        </span>
        <span className="text-end">
          <Amount row={row} currencies={currencies} className="block text-theme-sm font-medium" />
          {row.receipt_amount !== null && (
            <span className="block text-theme-xs text-gray-500 tabular-nums">
              {row.receipt_currency
                ? formatMoney(
                    row.receipt_amount,
                    row.receipt_currency,
                    decimalsOf(currencies, row.receipt_currency),
                  )
                : row.receipt_amount}
            </span>
          )}
        </span>
      </button>
    </li>
  );
}

function Inbox({
  rows,
  accounts,
  categories,
  currencies,
  onOpen,
}: {
  rows: InboxRow[];
  accounts: Account[];
  categories: Category[];
  currencies: ReturnType<typeof useCurrencies>['data'];
  onOpen: (row: Transaction) => void;
}) {
  const confirm = useConfirmTransaction();
  const remove = useDeleteTransaction();
  const [asking, setAsking] = useState<InboxRow[] | null>(null);
  const post = (list: InboxRow[]) => {
    if (list.some((r) => r.possible_duplicate)) {
      setAsking(list);
      return;
    }
    list.forEach((r) => confirm.mutate(r.id));
  };

  if (rows.length === 0) {
    return (
      <p className={cn(cardClass, 'text-center text-theme-sm text-gray-500')}>
        The inbox is empty.
      </p>
    );
  }
  return (
    <div className={cardClass}>
      <div className="flex flex-wrap items-center justify-between gap-3">
        <p className="text-theme-sm text-gray-500">
          Rows a parse or an agent proposed. They count once you post them.
        </p>
        <button
          type="button"
          onClick={() => post(rows)}
          disabled={confirm.isPending}
          className={cn(primaryButton, 'px-3 py-2')}
        >
          Post all {rows.length}
        </button>
      </div>
      <ul className="mt-3 divide-y divide-gray-100 dark:divide-white/5">
        {rows.map((r) => (
          <li key={r.id} className="flex flex-wrap items-center gap-3 py-2.5">
            <button type="button" onClick={() => onOpen(r)} className="min-w-0 flex-1 text-start">
              <span className="block truncate text-theme-sm text-gray-800 dark:text-white/90">
                {r.name}
              </span>
              <span className="block truncate text-theme-xs text-gray-500">
                {r.date} · {accounts.find((a) => a.id === r.account_id)?.name} ·{' '}
                {categories.find((c) => c.id === r.category_id)?.name ?? 'no category'}
                {r.possible_duplicate && (
                  <span className="ms-1 text-warning-600">· possible duplicate</span>
                )}
              </span>
            </button>
            <Amount row={r} currencies={currencies} className="text-theme-sm font-medium" />
            <div className="flex gap-2">
              <button
                type="button"
                onClick={() => post([r])}
                disabled={confirm.isPending}
                className={cn(secondaryButton, 'px-3 py-2')}
              >
                Post
              </button>
              <button
                type="button"
                onClick={() => remove.mutate(r.id)}
                disabled={remove.isPending}
                className={cn(secondaryButton, 'px-3 py-2')}
              >
                Discard
              </button>
            </div>
          </li>
        ))}
      </ul>
      {(confirm.error || remove.error) && (
        <p role="alert" className="mt-3 text-theme-sm text-error-500">
          {confirm.error ?? remove.error}
        </p>
      )}
      {asking && (
        <ConfirmDialog
          title="Post a possible duplicate?"
          description="A posted row on the same account with the same amount is within a day of it. Post anyway?"
          confirmLabel="Post"
          onConfirm={() => {
            asking.forEach((r) => confirm.mutate(r.id));
            setAsking(null);
          }}
          onClose={() => setAsking(null)}
        />
      )}
    </div>
  );
}

/** The ledger of a period, the inbox, and the add form. */
export function MoneyLedgerPage() {
  const [params, setParams] = useSearchParams();
  const { kind, date } = periodFromSearch(params, todayLocal());
  const range = periodRange(kind, date);
  const [view, setView] = useState<'ledger' | 'inbox'>(
    params.get('view') === 'inbox' ? 'inbox' : 'ledger',
  );
  const [account, setAccount] = useState(params.get('account') ?? '');
  const [category, setCategory] = useState(params.get('category') ?? '');
  const [type, setType] = useState(params.get('type') ?? '');
  const [text, setText] = useState('');
  const [q, setQ] = useState('');
  useEffect(() => {
    const t = setTimeout(() => setQ(text.trim()), 300);
    return () => clearTimeout(t);
  }, [text]);
  const [adding, setAdding] = useState(false);
  const [editing, setEditing] = useState<Transaction | null>(null);

  const currencies = useCurrencies().data;
  const accounts = useAccounts().data ?? [];
  const categories = useCategories(true).data ?? [];
  const inbox = useInbox();
  const list = useTransactions({
    from: range.from,
    to: range.to,
    account: account || undefined,
    category: category || undefined,
    type: type || undefined,
    q: q || undefined,
  });
  const rows = useMemo(() => list.data?.pages.flatMap((p) => p.data) ?? [], [list.data]);
  const days = useMemo(() => groupByDay(rows), [rows]);
  const report = useReport(kind, date);
  const filtered = account !== '' || category !== '' || type !== '' || q !== '';
  const periodTotals = filtered
    ? totalsByCurrency(rows)
    : (report.data?.blocks ?? []).map((b) => ({
        currency: b.currency,
        income: b.income,
        expense: b.expense,
      }));

  const setPeriod = (k: PeriodKind, d: string) => {
    const p = new URLSearchParams(params);
    p.set('kind', k);
    p.set('date', d);
    setParams(p, { replace: true });
  };
  const inboxCount = inbox.data?.length ?? 0;

  return (
    <MoneyFrame
      title="Money"
      actions={
        <button type="button" onClick={() => setAdding(true)} className={primaryButton}>
          <Plus className="size-4" aria-hidden="true" />
          Add
        </button>
      }
    >
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div role="tablist" aria-label="Ledger or inbox" className="flex gap-1">
          {(['ledger', 'inbox'] as const).map((v) => (
            <button
              key={v}
              type="button"
              role="tab"
              aria-selected={view === v}
              onClick={() => setView(v)}
              className={cn(
                'rounded-lg px-3 py-2 text-theme-sm font-medium transition',
                view === v
                  ? 'bg-brand-50 text-brand-500 dark:bg-brand-500/12 dark:text-brand-400'
                  : 'text-gray-500 hover:bg-gray-100 dark:text-gray-400 dark:hover:bg-white/5',
              )}
            >
              {v === 'ledger' ? 'Posted' : 'Inbox'}
              {v === 'inbox' && inboxCount > 0 && (
                <span className="ms-1.5 rounded-full bg-brand-500 px-1.5 text-theme-xs text-white">
                  {inboxCount}
                </span>
              )}
            </button>
          ))}
        </div>
        {view === 'ledger' && <PeriodRow kind={kind} date={date} onChange={setPeriod} />}
      </div>

      {view === 'inbox' ? (
        inbox.isPending ? (
          <Placeholder className="h-32" />
        ) : inbox.isError ? (
          <LoadError error={inbox.error} onRetry={() => void inbox.refetch()} />
        ) : (
          <Inbox
            rows={inbox.data}
            accounts={accounts}
            categories={categories}
            currencies={currencies}
            onOpen={setEditing}
          />
        )
      ) : (
        <>
          <div className="flex flex-wrap gap-3">
            <select
              value={account}
              onChange={(e) => setAccount(e.target.value)}
              aria-label="Account"
              className={cn(inputClass, 'h-10 w-auto max-w-full')}
            >
              <option value="">All accounts</option>
              {accounts.map((a) => (
                <option key={a.id} value={a.id}>
                  {a.name}
                </option>
              ))}
            </select>
            <select
              value={type}
              onChange={(e) => setType(e.target.value)}
              aria-label="Type"
              className={cn(inputClass, 'h-10 w-auto max-w-full')}
            >
              <option value="">All types</option>
              <option value="expense">Expenses</option>
              <option value="income">Incomes</option>
              <option value="fx_adjustment">Bank adjustments</option>
            </select>
            <select
              value={category}
              onChange={(e) => setCategory(e.target.value)}
              aria-label="Category"
              className={cn(inputClass, 'h-10 w-auto max-w-full')}
            >
              <option value="">All categories</option>
              {(['expense', 'income'] as const).map((k) => (
                <optgroup key={k} label={k === 'expense' ? 'Expense' : 'Income'}>
                  {categories
                    .filter((c) => c.kind === k)
                    .map((c) => (
                      <option key={c.id} value={c.id}>
                        {c.name}
                        {c.archived ? ' (archived)' : ''}
                      </option>
                    ))}
                </optgroup>
              ))}
            </select>
            <input
              type="search"
              value={text}
              onChange={(e) => setText(e.target.value)}
              placeholder="Search name or merchant"
              aria-label="Search"
              className={cn(inputClass, 'h-10 max-w-xs')}
            />
          </div>
          {list.isPending ? (
            <Placeholder className="h-48" />
          ) : list.isError ? (
            <LoadError error={list.error} onRetry={() => void list.refetch()} />
          ) : days.length === 0 ? (
            <>
              <p className={cn(cardClass, 'text-center text-theme-sm text-gray-500')}>
                Nothing in this period.
              </p>
              <TransfersList
                from={range.from}
                to={range.to}
                account={account}
                accounts={accounts}
                currencies={currencies}
              />
            </>
          ) : (
            <>
              <div className={cardClass}>
                <p className="text-theme-xs font-medium text-gray-500 uppercase">
                  {filtered
                    ? `Shown rows, per currency${list.hasNextPage ? ' (more to load)' : ''}`
                    : 'Period, per currency'}
                </p>
                <div className="mt-1">
                  <CurrencyTotals totals={periodTotals} currencies={currencies} />
                </div>
              </div>
              {days.map((d) => (
                <section key={d.date} className={cardClass} aria-label={formatDay(d.date)}>
                  <div className="flex flex-wrap items-baseline justify-between gap-2">
                    <h2 className="text-theme-sm font-semibold text-gray-800 dark:text-white/90">
                      {formatDay(d.date)}
                    </h2>
                    <CurrencyTotals totals={d.totals} currencies={currencies} />
                  </div>
                  <ul className="mt-2 divide-y divide-gray-100 dark:divide-white/5">
                    {d.rows.map((r) => (
                      <Row
                        key={r.id}
                        row={r}
                        account={accounts.find((a) => a.id === r.account_id)}
                        category={categories.find((c) => c.id === r.category_id)}
                        currencies={currencies}
                        onOpen={() => setEditing(r)}
                      />
                    ))}
                  </ul>
                </section>
              ))}
              <TransfersList
                from={range.from}
                to={range.to}
                account={account}
                accounts={accounts}
                currencies={currencies}
              />
              {list.hasNextPage && (
                <button
                  type="button"
                  onClick={() => void list.fetchNextPage()}
                  disabled={list.isFetchingNextPage}
                  className={secondaryButton}
                >
                  {list.isFetchingNextPage ? 'Loading…' : 'Show more'}
                </button>
              )}
            </>
          )}
        </>
      )}
      {adding && (
        <Modal onClose={() => setAdding(false)} className="max-w-xl">
          <AddForm onClose={() => setAdding(false)} />
        </Modal>
      )}
      {editing && (
        <Modal onClose={() => setEditing(null)} className="max-w-lg">
          <TransactionEditor
            row={editing}
            accounts={accounts}
            categories={categories}
            onClose={() => setEditing(null)}
          />
        </Modal>
      )}
    </MoneyFrame>
  );
}
