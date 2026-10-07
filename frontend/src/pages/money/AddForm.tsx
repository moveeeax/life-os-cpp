import { useEffect, useMemo, useState, type FormEvent } from 'react';
import { Loader2 } from 'lucide-react';

import {
  useAcceptParse,
  useAccounts,
  useCategories,
  useCreateTransaction,
  useCreateTransfer,
  useMerchants,
  useMoneyParse,
  useMoneySettings,
} from '@/hooks/useMoney';
import {
  RECEIPT_MAX_BYTES,
  draftFromJob,
  draftProblem,
  linesToAccept,
  parseErrorText,
  scaleImage,
  isDay,
  todayLocal,
  transferNeedsReceived,
  type Account,
  type Category,
  type DraftLine,
} from '@/lib/ledger';
import { cn } from '@/lib/utils';

import {
  inputClass,
  labelClass,
  modalPanel,
  primaryButton,
  secondaryButton,
  textareaClass,
} from '../workout/styles';

type Tab = 'manual' | 'describe' | 'receipt' | 'transfer';

const num = (v: string): number | null => {
  const t = v.trim().replace(',', '.');
  if (t === '') return null;
  const n = Number(t);
  return Number.isFinite(n) ? n : null;
};

const LAST_ACCOUNT = 'money:last-account';
const readLast = (): string => {
  try {
    return localStorage.getItem(LAST_ACCOUNT) ?? '';
  } catch {
    return '';
  }
};
const writeLast = (id: string) => {
  try {
    localStorage.setItem(LAST_ACCOUNT, id);
  } catch {
    // a private window: the form just does not remember
  }
};

function AccountSelect({
  id,
  value,
  onChange,
  accounts,
  label = 'Account',
}: {
  id: string;
  value: string;
  onChange: (v: string) => void;
  accounts: Account[];
  label?: string;
}) {
  return (
    <div>
      <label htmlFor={id} className={labelClass}>
        {label}
      </label>
      <select
        id={id}
        value={value}
        onChange={(e) => onChange(e.target.value)}
        className={inputClass}
      >
        <option value="">Pick the account</option>
        {accounts.map((a) => (
          <option key={a.id} value={a.id}>
            {a.name} · {a.currency}
          </option>
        ))}
      </select>
    </div>
  );
}

function CategorySelect({
  id,
  value,
  onChange,
  categories,
  kind,
}: {
  id: string;
  value: string;
  onChange: (v: string) => void;
  categories: Category[];
  kind: 'income' | 'expense';
}) {
  return (
    <div>
      <label htmlFor={id} className={labelClass}>
        Category
      </label>
      <select
        id={id}
        value={value}
        onChange={(e) => onChange(e.target.value)}
        className={inputClass}
      >
        <option value="">Pick a category</option>
        {categories
          .filter((c) => c.kind === kind)
          .map((c) => (
            <option key={c.id} value={c.id}>
              {c.name}
            </option>
          ))}
      </select>
    </div>
  );
}

// ── manual ─────────────────────────────────────────────────────────────────

function ManualTab({
  onDone,
  accounts,
  categories,
}: {
  onDone: () => void;
  accounts: Account[];
  categories: Category[];
}) {
  const [type, setType] = useState<'expense' | 'income'>('expense');
  const [amount, setAmount] = useState('');
  const [account, setAccount] = useState(() => {
    const last = readLast();
    return accounts.some((a) => a.id === last) ? last : '';
  });
  const [merchant, setMerchant] = useState('');
  const [name, setName] = useState('');
  const [category, setCategory] = useState('');
  const [date, setDate] = useState(todayLocal());
  const [receiptAmount, setReceiptAmount] = useState('');
  const [receiptCurrency, setReceiptCurrency] = useState('');
  const [note, setNote] = useState('');
  const [q, setQ] = useState('');
  const create = useCreateTransaction(() => {
    writeLast(account);
    onDone();
  });

  useEffect(() => {
    const t = setTimeout(() => setQ(merchant.trim()), 250);
    return () => clearTimeout(t);
  }, [merchant]);
  const suggestions = useMerchants(q);

  const a = num(amount);
  const ra = num(receiptAmount);
  const receiptBad =
    (receiptAmount.trim() !== '' || receiptCurrency.trim() !== '') &&
    (ra === null || ra <= 0 || !/^[A-Z]{3}$/.test(receiptCurrency.trim()));
  const ok =
    !!account &&
    a !== null &&
    a > 0 &&
    !!category &&
    (name.trim() || merchant.trim()) &&
    !receiptBad &&
    isDay(date);

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (!ok) return;
    create.mutate({
      type,
      date,
      account_id: account,
      amount: a!,
      merchant: merchant.trim(),
      name: (name.trim() || merchant.trim()).slice(0, 200),
      category_id: category,
      receipt_amount: receiptAmount.trim() ? ra : null,
      receipt_currency: receiptCurrency.trim() || null,
      note,
      source: 'manual',
      status: 'posted',
    });
  };

  return (
    <form onSubmit={submit} className="flex flex-col gap-4">
      <div role="radiogroup" aria-label="Direction" className="flex gap-2">
        {(['expense', 'income'] as const).map((t) => (
          <button
            key={t}
            type="button"
            role="radio"
            aria-checked={type === t}
            onClick={() => {
              setType(t);
              setCategory('');
            }}
            className={cn(
              secondaryButton,
              'px-3 py-2',
              type === t && 'border-brand-500 text-brand-500',
            )}
          >
            {t === 'expense' ? 'Expense' : 'Income'}
          </button>
        ))}
      </div>
      <div className="grid grid-cols-1 gap-4 sm:grid-cols-2">
        <div>
          <label htmlFor="m-amount" className={labelClass}>
            Amount, in the account's currency
          </label>
          <input
            id="m-amount"
            inputMode="decimal"
            value={amount}
            onChange={(e) => setAmount(e.target.value)}
            autoFocus
            className={inputClass}
          />
        </div>
        <AccountSelect id="m-account" value={account} onChange={setAccount} accounts={accounts} />
        <div className="relative sm:col-span-2">
          <label htmlFor="m-merchant" className={labelClass}>
            Merchant
          </label>
          <input
            id="m-merchant"
            value={merchant}
            onChange={(e) => setMerchant(e.target.value)}
            maxLength={200}
            autoComplete="off"
            className={inputClass}
          />
          {suggestions.data &&
            suggestions.data.length > 0 &&
            merchant.trim() !== suggestions.data[0].display_name && (
              <ul className="mt-1 flex flex-wrap gap-1" aria-label="Known merchants">
                {suggestions.data.slice(0, 5).map((m) => (
                  <li key={m.merchant_key}>
                    <button
                      type="button"
                      onClick={() => {
                        setMerchant(m.display_name);
                        if (
                          m.category_id &&
                          categories.some((c) => c.id === m.category_id && c.kind === type)
                        )
                          setCategory(m.category_id);
                        if (m.account_id && !account) setAccount(m.account_id);
                      }}
                      className="rounded-full border border-gray-200 px-2.5 py-1 text-theme-xs text-gray-700 hover:bg-gray-50 dark:border-gray-700 dark:text-gray-300 dark:hover:bg-white/5"
                    >
                      {m.display_name}
                    </button>
                  </li>
                ))}
              </ul>
            )}
        </div>
        <div className="sm:col-span-2">
          <label htmlFor="m-name" className={labelClass}>
            Name (what it was; the merchant when empty)
          </label>
          <input
            id="m-name"
            value={name}
            onChange={(e) => setName(e.target.value)}
            maxLength={200}
            className={inputClass}
          />
        </div>
        <CategorySelect
          id="m-category"
          value={category}
          onChange={setCategory}
          categories={categories}
          kind={type}
        />
        <div>
          <label htmlFor="m-date" className={labelClass}>
            Date
          </label>
          <input
            id="m-date"
            type="date"
            value={date}
            onChange={(e) => setDate(e.target.value)}
            className={inputClass}
          />
        </div>
        <div>
          <label htmlFor="m-ra" className={labelClass}>
            Receipt amount (optional)
          </label>
          <input
            id="m-ra"
            inputMode="decimal"
            value={receiptAmount}
            onChange={(e) => setReceiptAmount(e.target.value)}
            className={inputClass}
          />
        </div>
        <div>
          <label htmlFor="m-rc" className={labelClass}>
            Receipt currency
          </label>
          <input
            id="m-rc"
            value={receiptCurrency}
            onChange={(e) => setReceiptCurrency(e.target.value.toUpperCase())}
            maxLength={3}
            placeholder="THB"
            className={inputClass}
          />
        </div>
        <div className="sm:col-span-2">
          <label htmlFor="m-note" className={labelClass}>
            Note
          </label>
          <input
            id="m-note"
            value={note}
            onChange={(e) => setNote(e.target.value)}
            maxLength={2000}
            className={inputClass}
          />
        </div>
      </div>
      {(create.error || receiptBad) && (
        <p role="alert" className="text-theme-sm text-error-500">
          {create.error ?? 'The receipt needs both an amount and a three-letter currency code.'}
        </p>
      )}
      <div className="flex justify-end">
        <button type="submit" disabled={create.isPending || !ok} className={primaryButton}>
          {create.isPending ? 'Saving…' : 'Add'}
        </button>
      </div>
    </form>
  );
}

// ── describe and receipt: the parse ────────────────────────────────────────

function ParseLines({
  lines,
  setLines,
  accounts,
  categories,
}: {
  lines: DraftLine[];
  setLines: (f: (ls: DraftLine[]) => DraftLine[]) => void;
  accounts: Account[];
  categories: Category[];
}) {
  const edit = (key: string, patch: Partial<DraftLine>) =>
    setLines((ls) => ls.map((l) => (l.key === key ? { ...l, ...patch } : l)));
  return (
    <ul className="flex flex-col gap-3">
      {lines.map((l, i) => (
        <li key={l.key} className="rounded-lg border border-gray-200 p-3 dark:border-gray-700">
          <div className="flex items-start gap-2">
            <input
              type="checkbox"
              checked={l.selected}
              onChange={(e) => edit(l.key, { selected: e.target.checked })}
              aria-label={`Include line ${i + 1}`}
              className="mt-3"
            />
            <div className="grid flex-1 grid-cols-2 gap-2 sm:grid-cols-4">
              <input
                value={l.name}
                onChange={(e) => edit(l.key, { name: e.target.value })}
                aria-label={`Line ${i + 1} name`}
                maxLength={200}
                className={cn(inputClass, 'col-span-2 h-10')}
              />
              <input
                inputMode="decimal"
                value={l.amount}
                onChange={(e) => edit(l.key, { amount: e.target.value })}
                aria-label={`Line ${i + 1} amount`}
                className={cn(inputClass, 'h-10')}
              />
              <input
                type="date"
                value={l.date}
                onChange={(e) => edit(l.key, { date: e.target.value })}
                aria-label={`Line ${i + 1} date`}
                className={cn(inputClass, 'h-10')}
              />
              <select
                value={l.account_id ?? ''}
                onChange={(e) => edit(l.key, { account_id: e.target.value || null })}
                aria-label={`Line ${i + 1} account`}
                aria-invalid={l.selected && !l.account_id ? true : undefined}
                className={cn(inputClass, 'col-span-2 h-10')}
              >
                <option value="">Pick the account</option>
                {accounts.map((a) => (
                  <option key={a.id} value={a.id}>
                    {a.name} · {a.currency}
                  </option>
                ))}
              </select>
              <select
                value={l.category_id ?? ''}
                onChange={(e) => edit(l.key, { category_id: e.target.value || null })}
                aria-label={`Line ${i + 1} category`}
                className={cn(inputClass, 'col-span-2 h-10')}
              >
                <option value="">Pick a category</option>
                {categories
                  .filter((c) => c.kind === l.type)
                  .map((c) => (
                    <option key={c.id} value={c.id}>
                      {c.name}
                    </option>
                  ))}
              </select>
            </div>
          </div>
          <p className="mt-1 ps-6 text-theme-xs text-gray-500">
            {l.type === 'income' ? 'Income' : 'Expense'}
            {l.merchant && ` · ${l.merchant}`}
            {l.receipt_amount && ` · receipt ${l.receipt_amount} ${l.receipt_currency}`}
            {l.note && ` · ${l.note}`}
          </p>
        </li>
      ))}
    </ul>
  );
}

function ParseTab({
  kind,
  onDone,
  accounts,
  categories,
}: {
  kind: 'describe' | 'receipt';
  onDone: () => void;
  accounts: Account[];
  categories: Category[];
}) {
  const parse = useMoneyParse();
  const [text, setText] = useState('');
  const [photoProblem, setPhotoProblem] = useState<string | null>(null);
  const [lines, setLines] = useState<DraftLine[]>([]);
  const accept = useAcceptParse(parse.job?.id, onDone);

  const doneId = parse.state === 'done' ? parse.job?.id : undefined;
  useEffect(() => {
    if (doneId && parse.job)
      setLines(draftFromJob(parse.job, new Map(categories.map((c) => [c.id, c.kind]))));
  }, [doneId, parse.job, categories]);

  const problem = draftProblem(lines);
  const selected = lines.filter((l) => l.selected);

  const onPhoto = async (file: File | undefined) => {
    setPhotoProblem(null);
    if (!file) return;
    try {
      const scaled = await scaleImage(file);
      if (scaled.bytes > RECEIPT_MAX_BYTES) {
        setPhotoProblem('The photo is still over 4 MB after scaling.');
        return;
      }
      void parse.start({ kind: 'receipt', image: scaled.dataUrl, hint_date: todayLocal() });
    } catch {
      setPhotoProblem('This file is not an image the browser can read.');
    }
  };

  if (parse.state === 'starting' || parse.state === 'running') {
    return (
      <div className="flex flex-col items-center gap-3 py-6 text-center">
        <Loader2 className="size-6 animate-spin text-brand-500" aria-hidden="true" />
        <p className="text-theme-sm text-gray-500">
          {parse.state === 'starting'
            ? 'Sending…'
            : kind === 'receipt'
              ? 'Reading the receipt…'
              : 'Reading the text…'}
        </p>
        <button type="button" onClick={parse.cancel} className={secondaryButton}>
          Cancel
        </button>
      </div>
    );
  }

  if (parse.state === 'done') {
    return (
      <div className="flex flex-col gap-3">
        <p className="text-theme-sm text-gray-500">
          Check the lines; they go to the inbox, where you post them.
        </p>
        <ParseLines lines={lines} setLines={setLines} accounts={accounts} categories={categories} />
        {(accept.error || problem) && (
          <p role="alert" className="text-theme-sm text-error-500">
            {accept.error ?? problem}
          </p>
        )}
        <div className="flex flex-wrap justify-end gap-3">
          <button type="button" onClick={parse.cancel} className={secondaryButton}>
            Start over
          </button>
          <button
            type="button"
            disabled={accept.isPending || selected.length === 0 || problem !== null}
            onClick={() => accept.mutate(linesToAccept(lines))}
            className={primaryButton}
          >
            {accept.isPending ? 'Adding…' : `Add ${selected.length} to inbox`}
          </button>
        </div>
      </div>
    );
  }

  const failed = parse.state === 'failed' && (
    <p role="alert" className="text-theme-sm text-error-500">
      {parseErrorText(parse.error)}
    </p>
  );

  if (kind === 'receipt') {
    return (
      <div className="flex flex-col gap-3">
        <label htmlFor="r-photo" className={labelClass}>
          A photo of the receipt
        </label>
        <input
          id="r-photo"
          type="file"
          accept="image/*"
          capture="environment"
          onChange={(e) => {
            void onPhoto(e.target.files?.[0]);
            e.target.value = '';
          }}
          className="text-theme-sm text-gray-700 dark:text-gray-300"
        />
        <p className="text-theme-xs text-gray-500">Scaled in the browser before it is sent.</p>
        {photoProblem && (
          <p role="alert" className="text-theme-sm text-error-500">
            {photoProblem}
          </p>
        )}
        {failed}
      </div>
    );
  }

  return (
    <form
      onSubmit={(e) => {
        e.preventDefault();
        if (text.trim())
          void parse.start({ kind: 'text', text: text.trim(), hint_date: todayLocal() });
      }}
      className="flex flex-col gap-3"
    >
      <label htmlFor="d-text" className={labelClass}>
        A bank mail, an SMS or a list of purchases
      </label>
      <textarea
        id="d-text"
        value={text}
        onChange={(e) => setText(e.target.value)}
        rows={6}
        maxLength={8000}
        placeholder="Kaspi Gold *8880: purchase 13 275.61 KZT, BIG C PHUKET"
        autoFocus
        className={textareaClass}
      />
      {failed}
      <div className="flex justify-end">
        <button type="submit" disabled={!text.trim()} className={primaryButton}>
          {parse.state === 'failed' ? 'Try again' : 'Parse'}
        </button>
      </div>
    </form>
  );
}

// ── transfer ───────────────────────────────────────────────────────────────

function TransferTab({ onDone, accounts }: { onDone: () => void; accounts: Account[] }) {
  const [from, setFrom] = useState('');
  const [to, setTo] = useState('');
  const [sent, setSent] = useState('');
  const [received, setReceived] = useState('');
  const [fee, setFee] = useState('');
  const [date, setDate] = useState(todayLocal());
  const [note, setNote] = useState('');
  const create = useCreateTransfer(onDone);
  const fromCur = accounts.find((a) => a.id === from)?.currency;
  const toCur = accounts.find((a) => a.id === to)?.currency;
  const needsReceived = transferNeedsReceived(fromCur, toCur);
  const s = num(sent);
  const r = num(received);
  const f = num(fee);
  const ok =
    !!from &&
    !!to &&
    from !== to &&
    s !== null &&
    s > 0 &&
    (!needsReceived || (r !== null && r > 0)) &&
    (fee.trim() === '' || (f !== null && f >= 0)) &&
    isDay(date);

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (!ok) return;
    const fromName = accounts.find((a) => a.id === from)?.name ?? '';
    const toName = accounts.find((a) => a.id === to)?.name ?? '';
    create.mutate({
      date,
      from_account_id: from,
      to_account_id: to,
      amount_sent: s!,
      amount_received: needsReceived ? r : null,
      fee: fee.trim() === '' ? null : f,
      name: `${fromName} → ${toName}`.slice(0, 200),
      note,
    });
  };

  return (
    <form onSubmit={submit} className="flex flex-col gap-4">
      <div className="grid grid-cols-1 gap-4 sm:grid-cols-2">
        <AccountSelect
          id="t-from"
          value={from}
          onChange={(v) => {
            setFrom(v);
            if (v === to) setTo('');
          }}
          accounts={accounts}
          label="From"
        />
        <AccountSelect
          id="t-to"
          value={to}
          onChange={setTo}
          accounts={accounts.filter((a) => a.id !== from)}
          label="To"
        />
        <div>
          <label htmlFor="t-sent" className={labelClass}>
            Sent{fromCur ? `, ${fromCur}` : ''}
          </label>
          <input
            id="t-sent"
            inputMode="decimal"
            value={sent}
            onChange={(e) => setSent(e.target.value)}
            className={inputClass}
          />
        </div>
        {needsReceived && (
          <div>
            <label htmlFor="t-received" className={labelClass}>
              Received, {toCur}
            </label>
            <input
              id="t-received"
              inputMode="decimal"
              value={received}
              onChange={(e) => setReceived(e.target.value)}
              className={inputClass}
            />
          </div>
        )}
        <div>
          <label htmlFor="t-fee" className={labelClass}>
            Fee{fromCur ? `, ${fromCur}` : ''} (optional)
          </label>
          <input
            id="t-fee"
            inputMode="decimal"
            value={fee}
            onChange={(e) => setFee(e.target.value)}
            className={inputClass}
          />
        </div>
        <div>
          <label htmlFor="t-date" className={labelClass}>
            Date
          </label>
          <input
            id="t-date"
            type="date"
            value={date}
            onChange={(e) => setDate(e.target.value)}
            className={inputClass}
          />
        </div>
        <div className="sm:col-span-2">
          <label htmlFor="t-note" className={labelClass}>
            Note (receipt number, purpose)
          </label>
          <input
            id="t-note"
            value={note}
            onChange={(e) => setNote(e.target.value)}
            maxLength={2000}
            className={inputClass}
          />
        </div>
      </div>
      {needsReceived && s !== null && r !== null && r > 0 && (
        <p className="text-theme-xs text-gray-500 tabular-nums">
          Rate of this exchange: {(s / r).toFixed(4)} {fromCur} per {toCur}
        </p>
      )}
      {create.error && (
        <p role="alert" className="text-theme-sm text-error-500">
          {create.error}
        </p>
      )}
      <div className="flex justify-end">
        <button type="submit" disabled={create.isPending || !ok} className={primaryButton}>
          {create.isPending ? 'Saving…' : 'Add transfer'}
        </button>
      </div>
    </form>
  );
}

// ── the form ───────────────────────────────────────────────────────────────

/** Add a row: by hand, from a text, from a receipt photo, or a transfer. */
export function AddForm({ onClose }: { onClose: () => void }) {
  const accountsQuery = useAccounts();
  const accounts = accountsQuery.data ?? [];
  const categories = useCategories().data ?? [];
  const llm = useMoneySettings().data?.llm_available ?? false;
  const [tab, setTab] = useState<Tab>('manual');
  const tabs = useMemo(
    () =>
      (
        [
          ['manual', 'Manual'],
          ['describe', 'Describe'],
          ['receipt', 'Receipt'],
          ['transfer', 'Transfer'],
        ] as [Tab, string][]
      ).filter(([key]) => llm || (key !== 'describe' && key !== 'receipt')),
    [llm],
  );

  return (
    <div className={modalPanel} aria-labelledby="money-add-title">
      <h2 id="money-add-title" className="text-lg font-semibold">
        Add
      </h2>
      {accountsQuery.isPending ? (
        <p className="mt-3 text-theme-sm text-gray-500">Loading…</p>
      ) : accounts.length === 0 ? (
        <p className="mt-3 text-theme-sm text-gray-500">
          Create an account first, on the Accounts page.
        </p>
      ) : (
        <>
          <div role="tablist" aria-label="How to add" className="mt-3 flex gap-1 overflow-x-auto">
            {tabs.map(([key, text]) => (
              <button
                key={key}
                type="button"
                role="tab"
                aria-selected={tab === key}
                onClick={() => setTab(key)}
                className={cn(
                  'rounded-lg px-3 py-2 text-theme-sm font-medium whitespace-nowrap transition',
                  tab === key
                    ? 'bg-brand-50 text-brand-500 dark:bg-brand-500/12 dark:text-brand-400'
                    : 'text-gray-500 hover:bg-gray-100 dark:text-gray-400 dark:hover:bg-white/5',
                )}
              >
                {text}
              </button>
            ))}
          </div>
          <div className="mt-4">
            {tab === 'manual' && (
              <ManualTab onDone={onClose} accounts={accounts} categories={categories} />
            )}
            {(tab === 'describe' || tab === 'receipt') && (
              <ParseTab
                key={tab}
                kind={tab}
                onDone={onClose}
                accounts={accounts}
                categories={categories}
              />
            )}
            {tab === 'transfer' && <TransferTab onDone={onClose} accounts={accounts} />}
          </div>
        </>
      )}
    </div>
  );
}
