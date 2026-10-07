import { useState, type FormEvent } from 'react';
import { Plus } from 'lucide-react';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { Modal } from '@/components/Modal';
import {
  useCategories,
  useCurrencies,
  useDeleteCategory,
  useReport,
  useSaveCategory,
} from '@/hooks/useMoney';
import { budgetState, formatMoney, todayLocal, type Category, type Report } from '@/lib/ledger';
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

function CategoryForm({ category, onClose }: { category: Category | null; onClose: () => void }) {
  const currencies = useCurrencies().data ?? [];
  const [name, setName] = useState(category?.name ?? '');
  const [kind, setKind] = useState<'expense' | 'income'>(category?.kind ?? 'expense');
  const [flex, setFlex] = useState<'fixed' | 'variable'>(category?.flexibility ?? 'variable');
  const [budget, setBudget] = useState(
    category?.budget_max == null ? '' : String(category.budget_max),
  );
  const [budgetCurrency, setBudgetCurrency] = useState(category?.budget_currency ?? 'KZT');
  const [confirm, setConfirm] = useState(false);
  const [outcome, setOutcome] = useState<string | null>(null);
  const save = useSaveCategory(category?.id ?? null, onClose);
  const remove = useDeleteCategory((o) =>
    setOutcome(
      o === 'deleted'
        ? 'Deleted.'
        : 'Archived: rows refer to it, so it is hidden instead of removed.',
    ),
  );
  const b = budget.trim() === '' ? null : Number(budget.replace(',', '.'));
  const ok = name.trim() !== '' && (b === null || (Number.isFinite(b) && b > 0));

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (!ok) return;
    save.mutate({
      name: name.trim(),
      kind,
      flexibility: flex,
      budget_max: b,
      budget_currency: b === null ? null : budgetCurrency,
    });
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
      <form onSubmit={submit} className={modalPanel} aria-labelledby="category-form-title">
        <h2 id="category-form-title" className="text-lg font-semibold">
          {category ? 'Edit category' : 'New category'}
        </h2>
        <div className="mt-4 grid grid-cols-1 gap-4 sm:grid-cols-2">
          <div className="sm:col-span-2">
            <label htmlFor="cat-name" className={labelClass}>
              Name
            </label>
            <input
              id="cat-name"
              value={name}
              onChange={(e) => setName(e.target.value)}
              maxLength={60}
              className={inputClass}
            />
          </div>
          <div>
            <label htmlFor="cat-kind" className={labelClass}>
              Kind
            </label>
            <select
              id="cat-kind"
              value={kind}
              onChange={(e) => setKind(e.target.value as 'expense' | 'income')}
              className={inputClass}
            >
              <option value="expense">Expense</option>
              <option value="income">Income</option>
            </select>
          </div>
          <div>
            <label htmlFor="cat-flex" className={labelClass}>
              Flexibility
            </label>
            <select
              id="cat-flex"
              value={flex}
              onChange={(e) => setFlex(e.target.value as 'fixed' | 'variable')}
              className={inputClass}
            >
              <option value="variable">Variable</option>
              <option value="fixed">Fixed (rent, subscriptions)</option>
            </select>
          </div>
          <div>
            <label htmlFor="cat-budget" className={labelClass}>
              Monthly budget (optional)
            </label>
            <input
              id="cat-budget"
              inputMode="decimal"
              value={budget}
              onChange={(e) => setBudget(e.target.value)}
              className={inputClass}
            />
          </div>
          <div>
            <label htmlFor="cat-budget-currency" className={labelClass}>
              Budget currency
            </label>
            <select
              id="cat-budget-currency"
              value={budgetCurrency}
              onChange={(e) => setBudgetCurrency(e.target.value)}
              className={inputClass}
            >
              {currencies.map((c) => (
                <option key={c.code} value={c.code}>
                  {c.code}
                </option>
              ))}
            </select>
          </div>
        </div>
        <p className="mt-2 text-theme-xs text-gray-500">
          A budget is compared only with the spend in its own currency.
        </p>
        {(save.error || remove.error) && (
          <p role="alert" className="mt-3 text-theme-sm text-error-500">
            {save.error ?? remove.error}
          </p>
        )}
        <div className="mt-5 flex flex-wrap justify-between gap-3">
          {category ? (
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
              {save.isPending ? 'Saving…' : category ? 'Save' : 'Create'}
            </button>
          </div>
        </div>
      </form>
      {confirm && category && (
        <ConfirmDialog
          title="Delete this category?"
          description="A category with rows is archived instead."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => {
            setConfirm(false);
            remove.mutate(category.id);
          }}
          onClose={() => setConfirm(false)}
        />
      )}
    </>
  );
}

/** This month's spend of a category in its budget's currency. */
function spentIn(report: Report | undefined, category: Category): number {
  if (!report || !category.budget_currency) return 0;
  const block = report.blocks.find((b) => b.currency === category.budget_currency);
  return block?.categories.find((c) => c.category_id === category.id)?.spent ?? 0;
}

const BAR: Record<string, string> = {
  ok: 'bg-brand-500',
  near: 'bg-warning-500',
  over: 'bg-error-500',
};

/** Expense and income categories, each budget against this month's spend. */
export function MoneyCategoriesPage() {
  const categories = useCategories();
  const currencies = useCurrencies().data;
  const report = useReport('month', todayLocal()).data;
  const [open, setOpen] = useState<Category | 'new' | null>(null);

  return (
    <MoneyFrame
      title="Categories"
      actions={
        <button type="button" onClick={() => setOpen('new')} className={primaryButton}>
          <Plus className="size-4" aria-hidden="true" />
          New category
        </button>
      }
    >
      {categories.isPending ? (
        <Placeholder className="h-48" />
      ) : categories.isError ? (
        <LoadError error={categories.error} onRetry={() => void categories.refetch()} />
      ) : (
        (['expense', 'income'] as const).map((kind) => (
          <section key={kind} className={cardClass} aria-label={kind}>
            <h2 className="text-base font-semibold text-gray-800 dark:text-white/90">
              {kind === 'expense' ? 'Expenses' : 'Income'}
            </h2>
            <ul className="mt-2 divide-y divide-gray-100 dark:divide-white/5">
              {categories.data
                .filter((c) => c.kind === kind)
                .map((c) => {
                  const spent = spentIn(report, c);
                  const state = budgetState(spent, c.budget_max);
                  return (
                    <li key={c.id}>
                      <button
                        type="button"
                        onClick={() => setOpen(c)}
                        className="w-full py-2.5 text-start hover:bg-gray-50 dark:hover:bg-white/5"
                      >
                        <span className="flex items-baseline justify-between gap-3">
                          <span className="text-theme-sm text-gray-800 dark:text-white/90">
                            {c.name}
                            {c.flexibility === 'fixed' && (
                              <span className="ms-1.5 text-theme-xs text-gray-500">fixed</span>
                            )}
                          </span>
                          {c.budget_max != null && c.budget_currency && (
                            <span className="text-theme-xs text-gray-500 tabular-nums">
                              {formatMoney(
                                spent,
                                c.budget_currency,
                                decimalsOf(currencies, c.budget_currency),
                              )}{' '}
                              /{' '}
                              {formatMoney(
                                c.budget_max,
                                c.budget_currency,
                                decimalsOf(currencies, c.budget_currency),
                              )}
                            </span>
                          )}
                        </span>
                        {state && c.budget_max && (
                          <span className="mt-1 block h-1.5 overflow-hidden rounded-full bg-gray-100 dark:bg-white/10">
                            <span
                              className={cn('block h-full rounded-full', BAR[state])}
                              style={{ width: `${Math.min(100, (spent / c.budget_max) * 100)}%` }}
                            />
                          </span>
                        )}
                      </button>
                    </li>
                  );
                })}
            </ul>
          </section>
        ))
      )}
      {open && (
        <Modal onClose={() => setOpen(null)} className="max-w-xl">
          <CategoryForm category={open === 'new' ? null : open} onClose={() => setOpen(null)} />
        </Modal>
      )}
    </MoneyFrame>
  );
}
