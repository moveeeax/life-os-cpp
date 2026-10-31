import { memo, useEffect, useMemo, useRef, useState, type FormEvent, type ReactNode } from 'react';
import { useSearchParams } from 'react-router';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { Modal } from '@/components/Modal';
import {
  useAccounts,
  useCategories,
  useConfirmTransactions,
  useCurrencies,
  useDeleteTransaction,
  useInbox,
  useReport,
  useTransaction,
  useTransactions,
  useTransfers,
  useUpdateTransaction,
} from '@/hooks/useMoney';
import {
  decimalsOf,
  filtersFromSearch,
  formatDay,
  formatMoney,
  groupByDay,
  isDay,
  periodFromSearch,
  periodRange,
  todayLocal,
  totalsByCurrency,
  withSearch,
  type Account,
  type Category,
  type InboxRow,
  type PeriodKind,
  type Transaction,
  type TransactionPatch,
  type Transfer,
} from '@/lib/ledger';
import { parseAmount } from '@/lib/amounts';
import { convertSum, groupByWeek, type Rates, type Week } from '@/lib/ledger/weeks';
import { formatTotal } from '@/lib/networth';

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
import { MonthStrip } from './MonthStrip';
import { TransferRow } from './Transfers';
import './ledger.css';

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

/**
 * One ledger row. Memoized: with a few hundred rows loaded, a change elsewhere
 * on the page (a filter, the editor) does not render every row again. Its
 * props are the cached row, lookups from id maps and a stable onOpen.
 */
// Categories have no colour of their own: a calm one by their position.
const CATEGORY_COLORS = [
  '#6ee7b7',
  '#f0abfc',
  '#fcd34d',
  '#7dd3fc',
  '#fca5a5',
  '#c4b5fd',
  '#94a3b8',
  '#fdba74',
];
function categoryColor(c: Category): string {
  return CATEGORY_COLORS[Math.abs(c.position) % CATEGORY_COLORS.length];
}

type Item = { kind: 'row'; row: Transaction } | { kind: 'transfer'; transfer: Transfer };

/** Rows (latest day first) with the transfers of the same span put in by their day. */
function withTransfersByDay(rows: Transaction[], transfers: Transfer[]): Item[] {
  const rest = [...transfers].sort((a, b) => b.date.localeCompare(a.date));
  const out: Item[] = [];
  for (const row of rows) {
    while (rest.length > 0 && rest[0].date > row.date) {
      out.push({ kind: 'transfer', transfer: rest.shift()! });
    }
    out.push({ kind: 'row', row });
  }
  for (const t of rest) out.push({ kind: 'transfer', transfer: t });
  return out;
}

/** A week of the period: its dates, its row count and its spend in one currency; rows on click. */
function WeekGroup({
  week,
  count,
  open,
  current,
  onToggle,
  rates,
  target,
  currencies,
  children,
}: {
  week: Week;
  /** Rows and transfers of the week. */
  count: number;
  open: boolean;
  current: boolean;
  onToggle: () => void;
  rates: Rates | null;
  target: string;
  currencies: ReturnType<typeof useCurrencies>['data'];
  children: ReactNode;
}) {
  const label =
    week.from === week.to
      ? formatDay(week.from, { weekday: false })
      : `${formatDay(week.from, { weekday: false })} – ${week.to.slice(8).replace(/^0/, '')}`;
  const conv = (pick: 'expense' | 'income') =>
    rates
      ? convertSum(
          week.totals.map((t) => ({ currency: t.currency, amount: t[pick] })),
          rates,
          target,
        )
      : null;
  const spent = conv('expense');
  const came = conv('income');
  // The week's spend; a week of income only shows that instead.
  const sum =
    spent && spent.amount > 0
      ? { sign: '−', ...spent }
      : came && came.amount > 0
        ? { sign: '+', ...came }
        : null;
  return (
    <section className="l-week" data-open={open ? '' : undefined}>
      <button type="button" className="l-week-head" aria-expanded={open} onClick={onToggle}>
        <span className="l-week-name" data-num-ok>
          {label}
          {current && <span className="l-this"> · this week</span>}
        </span>
        <span className="l-week-meta" data-num-ok>
          {count}
          {sum ? (
            <span
              className="num"
              title={sum.partial ? 'Some currencies have no rate and are left out' : undefined}
            >
              {' · '}≈ {sum.sign}
              {formatTotal(sum.amount)} {target}
              {sum.partial ? '*' : ''}
            </span>
          ) : (
            !rates && (
              <>
                {' · '}
                <CurrencyTotals totals={week.totals} currencies={currencies} />
              </>
            )
          )}
        </span>
      </button>
      {open && <div className="k-group-rows">{children}</div>}
    </section>
  );
}

const LedgerRow = memo(function LedgerRow({
  row,
  account,
  category,
  onOpen,
  currencies,
}: {
  row: Transaction;
  account?: Account;
  category?: Category;
  onOpen: (row: Transaction) => void;
  currencies: ReturnType<typeof useCurrencies>['data'];
}) {
  const adjustment = row.type === 'fx_adjustment';
  const where = [category?.name, account?.name].filter(Boolean).join(' · ');
  return (
    <KitRow
      give={{ kind: 'transaction', id: row.id, name: row.name, merchant: row.merchant ?? null }}
      dot={category ? categoryColor(category) : 'transparent'}
      title={adjustment ? 'Bank recalculation' : row.name}
      right={
        <span className="l-right">
          <span className="l-date" data-num-ok>
            {formatDay(row.date, { weekday: false })}
          </span>{' '}
          · <Amount row={row} currencies={currencies} />
        </span>
      }
    >
      {where && <span>{where}</span>}
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
        <button type="button" className="k-link" onClick={() => onOpen(row)}>
          Edit or delete
        </button>
      </span>
    </KitRow>
  );
});

/**
 * The search field. It keeps the typed text and its 300 ms debounce to
 * itself and lifts only the settled query, so a keystroke renders this field,
 * not the ledger. It lives in the Filter sheet: closing the sheet sends what
 * was typed at once, and a cleared filter empties it.
 */
function SearchBox({ q, onSearch }: { q: string; onSearch: (q: string) => void }) {
  const [text, setText] = useState(q);
  const latest = useRef(q);
  useEffect(() => {
    const t = setTimeout(() => onSearch(text.trim()), 300);
    return () => clearTimeout(t);
  }, [text, onSearch]);
  useEffect(() => {
    if (q !== latest.current.trim()) {
      latest.current = q;
      setText(q);
    }
  }, [q]);
  useEffect(() => () => onSearch(latest.current.trim()), [onSearch]);
  return (
    <input
      type="search"
      value={text}
      onChange={(e) => {
        latest.current = e.target.value;
        setText(e.target.value);
      }}
      placeholder="Name or merchant"
    />
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
  // One mutation for the whole list: one pending state, one error
  // ("2 of 5 failed: …"), one refetch at the end.
  const confirm = useConfirmTransactions();
  const remove = useDeleteTransaction();
  const [asking, setAsking] = useState<InboxRow[] | null>(null);
  const post = (list: InboxRow[]) => {
    if (list.some((r) => r.possible_duplicate)) {
      setAsking(list);
      return;
    }
    confirm.mutate(list.map((r) => r.id));
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
            confirm.mutate(asking.map((r) => r.id));
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
  // The view and the filters live in the URL: a reload, a shared link and
  // the back button keep them. A default value leaves the URL.
  const { view, account, category, type } = filtersFromSearch(params);
  const setView = (v: 'ledger' | 'inbox') =>
    setParams((p) => withSearch(p, { view: v === 'inbox' ? 'inbox' : '' }));
  const setFilter = (key: 'account' | 'category' | 'type', value: string) =>
    setParams((p) => withSearch(p, { [key]: value }), { replace: true });
  const setAccount = (v: string) => setFilter('account', v);
  const setCategory = (v: string) => setFilter('category', v);
  const setType = (v: string) => setFilter('type', v);
  const [q, setQ] = useState('');
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
  // Archived accounts too: their rows still need a name, and the Accounts page
  // links an archived account's rows here as ?account=<id>.
  const accountsData = useAccounts(true).data;
  const categoriesData = useCategories(true).data;
  const accounts = useMemo(() => accountsData ?? [], [accountsData]);
  const categories = useMemo(() => categoriesData ?? [], [categoriesData]);
  const accountById = useMemo(() => new Map(accounts.map((a) => [a.id, a])), [accounts]);
  const categoryById = useMemo(() => new Map(categories.map((c) => [c.id, c])), [categories]);
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
  // A month or a quarter reads by week; a week keeps its days.
  const byWeek = kind !== 'week';
  const weeks = useMemo(
    () => (byWeek ? groupByWeek(rows, range.from, range.to) : []),
    [byWeek, rows, range.from, range.to],
  );
  // The weeks count and sum every row of the period: load it all (up to 1,000 rows).
  const { hasNextPage, isFetchingNextPage, fetchNextPage } = list;
  useEffect(() => {
    if (byWeek && hasNextPage && !isFetchingNextPage && rows.length < 1000) void fetchNextPage();
  }, [byWeek, hasNextPage, isFetchingNextPage, fetchNextPage, rows.length]);
  // Transfers and exchanges sit in the same list by date; a category, type or
  // search filter leaves them out, since they have none of those.
  const transferList = useTransfers(range.from, range.to, account || undefined);
  const withTransfers = category === '' && type === '' && q === '';
  const transfers = useMemo(
    () => (withTransfers ? (transferList.data?.pages.flatMap((p) => p.data) ?? []) : []),
    [withTransfers, transferList.data],
  );
  const {
    hasNextPage: moreTransfers,
    isFetchingNextPage: fetchingTransfers,
    fetchNextPage: fetchTransfers,
  } = transferList;
  useEffect(() => {
    if (withTransfers && moreTransfers && !fetchingTransfers && transfers.length < 1000)
      void fetchTransfers();
  }, [withTransfers, moreTransfers, fetchingTransfers, fetchTransfers, transfers.length]);
  const transfersIn = (from: string, to: string) =>
    transfers.filter((t) => t.date >= from && t.date <= to);
  const today = todayLocal();
  const [openWeeks, setOpenWeeks] = useState<Record<string, boolean>>({});
  const weekOpen = (w: Week) => openWeeks[w.from] ?? (today >= w.from && today <= w.to);
  // A week period by day: the days with rows and those with only a transfer.
  const dayList = useMemo(() => {
    if (byWeek) return [];
    const have = new Set(days.map((d) => d.date));
    const extra = [...new Set(transfers.map((t) => t.date))]
      .filter((d) => !have.has(d))
      .map((date) => ({ date, rows: [] as Transaction[], totals: [] }));
    return [...days, ...extra].sort((a, b) => b.date.localeCompare(a.date));
  }, [byWeek, days, transfers]);
  const renderItem = (it: Item) =>
    it.kind === 'row' ? (
      <LedgerRow
        key={it.row.id}
        row={it.row}
        account={accountById.get(it.row.account_id)}
        category={it.row.category_id ? categoryById.get(it.row.category_id) : undefined}
        currencies={currencies}
        onOpen={setEditing}
      />
    ) : (
      <TransferRow
        key={it.transfer.id}
        transfer={it.transfer}
        accounts={accounts}
        currencies={currencies}
      />
    );
  const report = useReport(kind, date);
  const asIf = report.data?.as_if;
  const [byCurrency, setByCurrency] = useState(false);
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
    setParams((p) => withSearch(p, { account: '', category: '', type: '' }), { replace: true });
    setQ('');
  };

  return (
    <MoneyFrame
      title="Ledger"
      actions={
        <>
          <div className="k-tools-line">
            {view === 'ledger' ? (
              <PeriodRow kind={kind} date={date} onChange={setPeriod} />
            ) : (
              <button type="button" className="k-link" onClick={() => setView('ledger')}>
                ← Back to the ledger
              </button>
            )}
          </div>
          <span className="k-grow" />
          <div className="k-tools-line k-tools-end">
            {view === 'ledger' && (
              <Filters active={filterCount} onClear={clearFilters}>
                <label className="k-field">
                  <span>Account</span>
                  <select value={account} onChange={(e) => setAccount(e.target.value)}>
                    <option value="">All accounts</option>
                    {accounts.map((a) => (
                      <option key={a.id} value={a.id}>
                        {a.name}
                        {a.archived ? ' (archived)' : ''}
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
                  <SearchBox q={q} onSearch={setQ} />
                </label>
              </Filters>
            )}
            <button type="button" onClick={() => setAdding(true)} className={primaryButton}>
              Add
            </button>
          </div>
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
          {rowId && linked.isError && (
            <p role="status" className="k-sub">
              The linked row was not found. It may have been deleted.
            </p>
          )}
          {inboxCount > 0 && (
            <Banner action="Review" onAction={() => setView('inbox')}>
              <span data-num-ok>
                {inboxCount === 1 ? '1 row waits' : `${inboxCount} rows wait`}
              </span>{' '}
              in Inbox
            </Banner>
          )}
          <MonthStrip
            year={Number(range.from.slice(0, 4))}
            month={kind === 'month' ? Number(range.from.slice(5, 7)) : null}
            today={today}
            onPick={(d) => setPeriod('month', d)}
          />
          {!filtered && asIf && (asIf.expense > 0 || asIf.income > 0) && (
            <div className="l-sum">
              <span>
                <span className="l-k">Spent</span>{' '}
                <span className="l-v num">
                  ≈ {formatTotal(asIf.expense)} {asIf.currency}
                </span>
              </span>
              {asIf.income > 0 && (
                <span>
                  <span className="l-k">In</span>{' '}
                  <span className="l-v l-in num">
                    ≈ {formatTotal(asIf.income)} {asIf.currency}
                  </span>
                </span>
              )}
              {periodTotals.length > 1 && (
                <button
                  type="button"
                  className="l-why"
                  aria-expanded={byCurrency}
                  onClick={() => setByCurrency(!byCurrency)}
                  data-num-ok
                >
                  {periodTotals.length} currencies
                </button>
              )}
            </div>
          )}
          {periodTotals.length > 0 && (filtered || !asIf || byCurrency) && (
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
              {days.length === 0 && transfers.length === 0 && (
                <p className="k-empty">Nothing in this period.</p>
              )}
              {byWeek
                ? weeks.map((w) => {
                    const items = withTransfersByDay(w.rows, transfersIn(w.from, w.to));
                    if (items.length === 0) return null;
                    return (
                      <WeekGroup
                        key={w.from}
                        week={w}
                        count={items.length}
                        open={weekOpen(w)}
                        current={today >= w.from && today <= w.to}
                        onToggle={() => setOpenWeeks((o) => ({ ...o, [w.from]: !weekOpen(w) }))}
                        rates={(asIf?.rates as Rates | undefined) ?? null}
                        target={asIf?.currency ?? ''}
                        currencies={currencies}
                      >
                        {items.map(renderItem)}
                      </WeekGroup>
                    );
                  })
                : dayList.map((d) => (
                    <Group
                      key={d.date}
                      label={formatDay(d.date)}
                      total={<CurrencyTotals totals={d.totals} currencies={currencies} />}
                    >
                      {withTransfersByDay(d.rows, transfersIn(d.date, d.date)).map(renderItem)}
                    </Group>
                  ))}

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
