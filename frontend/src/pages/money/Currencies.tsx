import { useState, type FormEvent } from 'react';
import { Plus } from 'lucide-react';

import { Modal } from '@/components/Modal';
import { useCurrencies, useSaveCurrency } from '@/hooks/useMoney';
import { smallestUnit, type Currency, type CurrencyInput } from '@/lib/ledger';
import { cn } from '@/lib/utils';

import {
  cardClass,
  inputClass,
  labelClass,
  modalPanel,
  primaryButton,
  secondaryButton,
} from '../workout/styles';
import { LoadError, MoneyFrame, Placeholder } from './frame';

type Role = Currency['role'];

const ROLES: { value: '' | 'primary' | 'local'; label: string }[] = [
  { value: '', label: 'None' },
  { value: 'primary', label: 'Primary (listed first)' },
  { value: 'local', label: 'Local (listed next)' },
];

function CurrencyForm({ currency, onClose }: { currency: Currency | null; onClose: () => void }) {
  const [code, setCode] = useState(currency?.code ?? '');
  const [name, setName] = useState(currency?.name ?? '');
  const [role, setRole] = useState<'' | 'primary' | 'local'>(currency?.role ?? '');
  const [archived, setArchived] = useState(currency?.archived ?? false);
  const [decimals, setDecimals] = useState('');
  const [minorUnit, setMinorUnit] = useState('');
  const save = useSaveCurrency(currency?.code ?? null, onClose);
  const upper = code.trim().toUpperCase();
  const d = decimals.trim() === '' ? null : Number(decimals);
  const ok =
    (currency !== null || /^[A-Z]{3}$/.test(upper)) &&
    (d === null || (Number.isInteger(d) && d >= 0 && d <= 8));

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (!ok) return;
    const common = { name: name.trim(), role: (role || null) as Role };
    if (currency) {
      save.mutate({ ...common, archived });
      return;
    }
    const body: CurrencyInput = { code: upper, ...common, archived: false };
    // A known code takes its unit from the server's table; another one names its own.
    if (d !== null) body.decimals = d;
    if (minorUnit.trim() !== '') body.minor_unit = minorUnit.trim();
    save.mutate(body);
  };

  return (
    <form onSubmit={submit} className={modalPanel} aria-labelledby="currency-form-title">
      <h2 id="currency-form-title" className="text-lg font-semibold">
        {currency ? `Edit ${currency.code}` : 'New currency'}
      </h2>
      <div className="mt-4 grid grid-cols-1 gap-4 sm:grid-cols-2">
        {currency ? (
          <p className="text-theme-sm text-gray-600 sm:col-span-2 dark:text-gray-400">
            Smallest amount: <span className="tabular-nums">{smallestUnit(currency)}</span>. It is
            fixed: every stored amount is a whole number of it.
          </p>
        ) : (
          <div>
            <label htmlFor="cur-code" className={labelClass}>
              Code
            </label>
            <input
              id="cur-code"
              value={code}
              onChange={(e) => setCode(e.target.value)}
              maxLength={3}
              autoCapitalize="characters"
              className={cn(inputClass, 'uppercase')}
            />
          </div>
        )}
        <div className={currency ? 'sm:col-span-2' : undefined}>
          <label htmlFor="cur-name" className={labelClass}>
            Name
          </label>
          <input
            id="cur-name"
            value={name}
            onChange={(e) => setName(e.target.value)}
            maxLength={60}
            className={inputClass}
          />
        </div>
        {!currency && (
          <>
            <div>
              <label htmlFor="cur-decimals" className={labelClass}>
                Decimals
              </label>
              <input
                id="cur-decimals"
                inputMode="numeric"
                value={decimals}
                onChange={(e) => setDecimals(e.target.value)}
                placeholder="from the ISO table"
                className={inputClass}
              />
            </div>
            <div>
              <label htmlFor="cur-unit" className={labelClass}>
                Smallest unit name
              </label>
              <input
                id="cur-unit"
                value={minorUnit}
                onChange={(e) => setMinorUnit(e.target.value)}
                maxLength={30}
                placeholder="from the ISO table"
                className={inputClass}
              />
            </div>
            <p className="text-theme-xs text-gray-500 sm:col-span-2">
              ISO codes and BTC come with their decimals and unit (2 and kopeck for RUB, 8 and
              satoshi for BTC). Fill both fields only for a code outside the table, such as XAU.
            </p>
          </>
        )}
        <div>
          <label htmlFor="cur-role" className={labelClass}>
            Role
          </label>
          <select
            id="cur-role"
            value={role}
            onChange={(e) => setRole(e.target.value as '' | 'primary' | 'local')}
            className={inputClass}
          >
            {ROLES.map((r) => (
              <option key={r.value} value={r.value}>
                {r.label}
              </option>
            ))}
          </select>
        </div>
        {currency && (
          <label className="flex items-center gap-2 self-end pb-2 text-theme-sm">
            <input
              type="checkbox"
              checked={archived}
              onChange={(e) => setArchived(e.target.checked)}
            />
            Archived (hidden from pickers)
          </label>
        )}
      </div>
      {save.error && (
        <p role="alert" className="mt-3 text-theme-sm text-error-500">
          {save.error}
        </p>
      )}
      <div className="mt-5 flex justify-end gap-3">
        <button type="button" onClick={onClose} className={secondaryButton}>
          Cancel
        </button>
        <button type="submit" disabled={save.isPending || !ok} className={primaryButton}>
          {save.isPending ? 'Saving…' : currency ? 'Save' : 'Add'}
        </button>
      </div>
    </form>
  );
}

/** The currencies amounts may be kept in, each with its smallest unit. */
export function MoneyCurrenciesPage() {
  const [showArchived, setShowArchived] = useState(false);
  const currencies = useCurrencies(showArchived);
  const [open, setOpen] = useState<Currency | 'new' | null>(null);

  return (
    <MoneyFrame
      title="Currencies"
      actions={
        <button type="button" onClick={() => setOpen('new')} className={primaryButton}>
          <Plus className="size-4" aria-hidden="true" />
          New currency
        </button>
      }
    >
      <section className={cardClass} aria-label="Currencies">
        <div className="flex items-center justify-between gap-3">
          <h2 className="text-base font-semibold text-gray-800 dark:text-white/90">
            Currencies and their smallest unit
          </h2>
          <label className="flex items-center gap-2 text-theme-sm text-gray-600 dark:text-gray-400">
            <input
              type="checkbox"
              checked={showArchived}
              onChange={(e) => setShowArchived(e.target.checked)}
            />
            Show archived
          </label>
        </div>
        {currencies.isPending ? (
          <Placeholder className="mt-3 h-48" />
        ) : currencies.isError ? (
          <LoadError error={currencies.error} onRetry={() => void currencies.refetch()} />
        ) : (
          <ul className="mt-2 divide-y divide-gray-100 dark:divide-white/5">
            {currencies.data.map((c) => (
              <li key={c.code}>
                <button
                  type="button"
                  onClick={() => setOpen(c)}
                  className={cn(
                    'flex w-full items-baseline justify-between gap-3 py-2.5 text-start hover:bg-gray-50 dark:hover:bg-white/5',
                    c.archived && 'opacity-60',
                  )}
                >
                  <span className="text-theme-sm text-gray-800 dark:text-white/90">
                    <span className="font-mono font-semibold">{c.code}</span>
                    {c.name && <span className="ms-2">{c.name}</span>}
                    {c.role && <span className="ms-1.5 text-theme-xs text-gray-500">{c.role}</span>}
                    {c.archived && (
                      <span className="ms-1.5 text-theme-xs text-gray-500">archived</span>
                    )}
                  </span>
                  <span className="text-theme-xs text-gray-500 tabular-nums">
                    {smallestUnit(c)}
                  </span>
                </button>
              </li>
            ))}
          </ul>
        )}
      </section>
      {open && (
        <Modal onClose={() => setOpen(null)} className="max-w-xl">
          <CurrencyForm currency={open === 'new' ? null : open} onClose={() => setOpen(null)} />
        </Modal>
      )}
    </MoneyFrame>
  );
}
