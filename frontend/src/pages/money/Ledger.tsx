import { useEffect, useMemo, useState, type FormEvent } from 'react';
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
  useTransaction,
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

import { Banner, Filters, Group, Row as KitRow } from '@/shell/kit';
import {
  dangerButton,
  inputClass,
  labelClass,
  modalPanel,
  primaryButton,
  secondaryButton,
} from '@/shell/kit/styles';
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

function LedgerRow({
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
    <KitRow
      title={adjustment ? 'Bank recalculation' : row.name}
      sub={[category?.name, account?.name].filter(Boolean).join(' · ')}
      right={<Amount row={row} currencies={currencies} />}
    >
      {row.merchant && row.merchant !== row.name && <span>Merchant {row.merchant}</span>}
      {row.receipt_amount !== null && (
        <span>
          Receipt{' '}
          <span className="num">
            {row.receipt_currency
              ? formatMoney(
                  row.receipt_amount,
                  row.receipt_currency,
                  decimalsOf(currencies, row.receipt_currency),
                )
              : row.receipt_amount}
          </span>
        </span>
      )}
      {row.note && <span>{row.note}</span>}
      <span className="k-actions">
        <button type="button" className="k-link" onClick={onOpen}>
          Edit or delete
        </button>
      </span>
    </KitRow>
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
    return <p className="k-empty">The inbox is empty.</p>;
  }
  return (
    <div>
      <div className="k-tools">
        <span className="k-sub k-grow">
          Rows a parse or an agent proposed. They count once you post them.
        </span>
        <button
          type="button"
          onClick={() => post(rows)}
          disabled={confirm.isPending}
          className={primaryButton}
        >
          Post all {rows.length}
        </button>
      </div>
      <div className="k-group-rows">
        {rows.map((r) => (
          <KitRow
            key={r.id}
            title={r.name}
            sub={
              <>
                {formatDay(r.date)} · {accounts.find((a) => a.id === r.account_id)?.name} ·{' '}
                {categories.find((c) => c.id === r.category_id)?.name ?? 'no category'}
                {r.possible_duplicate && <span className="k-warn-text"> · possible duplicate</span>}
              </>
            }
            right={<Amount row={r} currencies={currencies} />}
          >
            <span className="k-actions">
              <button
                type="button"
                className="k-link"
                onClick={() => post([r])}
                disabled={confirm.isPending}
              >
                Post
              </button>
              <button type="button" className="k-link" onClick={() => onOpen(r)}>
                Edit
              </button>
              <button
                type="button"
                className="k-link"
                onClick={() => remove.mutate(r.id)}
                disabled={remove.isPending}
              >
                Discard
              </button>
            </span>
          </KitRow>
        ))}
      </div>
      {(confirm.error || remove.error) && (
        <p role="alert" className="k-error">
          {String(confirm.error ?? remove.error)}
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
  // ?row=<id>: a link (from a task) that opens one row of the ledger.
  const rowId = params.get('row');
  const linked = useTransaction(rowId);
  useEffect(() => {
    if (linked.data && rowId) setEditing(linked.data);
  }, [linked.data, rowId]);
  const closeEditor = () => {
    setEditing(null);
    if (rowId) {
      const next = new URLSearchParams(params);
      next.delete('row');
      setParams(next, { replace: true });
    }
  };

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

  const filterCount = [account, category, type, q].filter((v) => v !== '').length;
  const clearFilters = () => {
    setAccount('');
    setCategory('');
    setType('');
    setText('');
  };

  return (
    <MoneyFrame
      title="Ledger"
      actions={
        <>
          {view === 'ledger' ? (
            <PeriodRow kind={kind} date={date} onChange={setPeriod} />
          ) : (
            <button type="button" className="k-link" onClick={() => setView('ledger')}>
              ← Back to the ledger
            </button>
          )}
          <span className="k-grow" />
          {view === 'ledger' && (
            <Filters active={filterCount} onClear={clearFilters}>
              <label className="k-field">
                <span>Account</span>
                <select value={account} onChange={(e) => setAccount(e.target.value)}>
                  <option value="">All accounts</option>
                  {accounts.map((a) => (
                    <option key={a.id} value={a.id}>
                      {a.name}
                    </option>
                  ))}
                </select>
              </label>
              <label className="k-field">
                <span>Type</span>
                <select value={type} onChange={(e) => setType(e.target.value)}>
                  <option value="">All types</option>
                  <option value="expense">Expenses</option>
                  <option value="income">Incomes</option>
                  <option value="fx_adjustment">Bank adjustments</option>
                </select>
              </label>
              <label className="k-field">
                <span>Category</span>
                <select value={category} onChange={(e) => setCategory(e.target.value)}>
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
              </label>
              <label className="k-field">
                <span>Search</span>
                <input
                  type="search"
                  value={text}
                  onChange={(e) => setText(e.target.value)}
                  placeholder="Name or merchant"
                />
              </label>
            </Filters>
          )}
          <button type="button" onClick={() => setAdding(true)} className={primaryButton}>
            Add
          </button>
        </>
      }
    >
      {view === 'inbox' ? (
        inbox.isPending ? (
          <Placeholder />
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
          {inboxCount > 0 && (
            <Banner action="Review" onAction={() => setView('inbox')}>
              <span data-num-ok>
                {inboxCount === 1 ? '1 row waits' : `${inboxCount} rows wait`}
              </span>{' '}
              in Inbox
            </Banner>
          )}
          {periodTotals.length > 0 && (
            <div className="k-sub">
              {filtered
                ? `Shown rows${list.hasNextPage ? ' (more to load)' : ''}: `
                : 'This period: '}
              <CurrencyTotals totals={periodTotals} currencies={currencies} />
            </div>
          )}
          {list.isPending ? (
            <Placeholder />
          ) : list.isError ? (
            <LoadError error={list.error} onRetry={() => void list.refetch()} />
          ) : (
            <>
              {days.length === 0 && <p className="k-empty">Nothing in this period.</p>}
              {days.map((d) => (
                <Group
                  key={d.date}
                  label={formatDay(d.date)}
                  total={<CurrencyTotals totals={d.totals} currencies={currencies} />}
                >
                  {d.rows.map((r) => (
                    <LedgerRow
                      key={r.id}
                      row={r}
                      account={accounts.find((a) => a.id === r.account_id)}
                      category={categories.find((c) => c.id === r.category_id)}
                      currencies={currencies}
                      onOpen={() => setEditing(r)}
                    />
                  ))}
                </Group>
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
        <Modal onClose={closeEditor} className="max-w-lg">
          <TransactionEditor
            row={editing}
            accounts={accounts}
            categories={categories}
            onClose={closeEditor}
          />
        </Modal>
      )}
    </MoneyFrame>
  );
}
