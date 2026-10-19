import { useState } from 'react';
import { useSearchParams } from 'react-router';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { useErrorToast } from '@/hooks/useErrorToast';
import { useClients, useDeleteEntry, useEntries } from '@/hooks/usePractice';
import { byMonth, entryTitle, monthLabel, todayLocal, type PracticeEntry } from '@/lib/practice';
import { Filters, Group, Num } from '@/shell/kit';

import { EntryRow } from './EntryRow';
import { LoadError, Placeholder, PracticeFrame, Tools } from './frame';
import { EntrySheet } from './Sheets';

/** Incomes and expenses of the practice, by month; each amount in its own currency. */
export function PracticeMoneyPage() {
  const today = todayLocal();
  const [params, setParams] = useSearchParams();
  const kind = (params.get('kind') ?? '') as '' | 'income' | 'expense';
  const client = params.get('client') ?? '';
  const month = params.get('month') ?? '';
  const entries = useEntries({ kind: kind || undefined, client, month });
  const clients = useClients().data ?? [];
  const [editing, setEditing] = useState<PracticeEntry | 'income' | 'expense' | null>(null);
  const [deleting, setDeleting] = useState<PracticeEntry | null>(null);
  const remove = useDeleteEntry(() => setDeleting(null));
  useErrorToast(remove.error);
  const setParam = (key: string, value: string) => {
    const next = new URLSearchParams(params);
    if (value) next.set(key, value);
    else next.delete(key);
    setParams(next, { replace: true });
  };
  const list = entries.data?.data ?? [];
  const total = entries.data?.total ?? 0;
  return (
    <PracticeFrame
      headline={month ? <span data-num-ok>{monthLabel(month)}</span> : 'Money of the practice'}
      sub="each amount in its own currency"
      actions={
        <>
          <Filters
            active={[kind, client, month].filter(Boolean).length}
            onClear={() => setParams(new URLSearchParams(), { replace: true })}
          >
            <label className="k-field">
              <span>Kind</span>
              <select value={kind} onChange={(e) => setParam('kind', e.target.value)}>
                <option value="">Incomes and expenses</option>
                <option value="income">Incomes</option>
                <option value="expense">Expenses</option>
              </select>
            </label>
            <label className="k-field">
              <span>Client</span>
              <select value={client} onChange={(e) => setParam('client', e.target.value)}>
                <option value="">Every client</option>
                {clients.map((c) => (
                  <option key={c.id} value={c.id}>
                    {c.name}
                  </option>
                ))}
              </select>
            </label>
            <label className="k-field">
              <span>Month</span>
              <input
                type="month"
                value={month}
                onChange={(e) => setParam('month', e.target.value)}
              />
            </label>
          </Filters>
          <Tools>
            <button type="button" className="k-btn" onClick={() => setEditing('expense')}>
              Expense
            </button>
            <button type="button" className="k-btn k-solid" onClick={() => setEditing('income')}>
              Income
            </button>
          </Tools>
        </>
      }
    >
      {entries.isPending ? (
        <Placeholder />
      ) : entries.isError ? (
        <LoadError error={entries.error} onRetry={() => void entries.refetch()} />
      ) : list.length === 0 ? (
        <p className="k-empty">
          {kind || client || month
            ? 'Nothing matches the filter.'
            : 'No incomes or expenses yet. Each one keeps its own currency.'}
        </p>
      ) : (
        <>
          {byMonth(list).map((g) => (
            <Group key={g.month} label={<span data-num-ok>{monthLabel(g.month)}</span>}>
              {g.rows.map((e) => (
                <EntryRow
                  key={e.id}
                  entry={e}
                  today={today}
                  onEdit={() => setEditing(e)}
                  onDelete={() => setDeleting(e)}
                />
              ))}
            </Group>
          ))}
          {total > list.length && (
            <p className="k-muted">
              The latest <Num>{list.length}</Num> of <Num>{total}</Num>; filter by month for the
              rest.
            </p>
          )}
        </>
      )}
      {editing && (
        <EntrySheet
          entry={typeof editing === 'string' ? undefined : editing}
          initial={typeof editing === 'string' ? { kind: editing } : undefined}
          onClose={() => setEditing(null)}
        />
      )}
      {deleting && (
        <ConfirmDialog
          title={`Delete ${entryTitle(deleting)}?`}
          description="The month totals change with it. This cannot be undone."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate(deleting.id)}
          onClose={() => setDeleting(null)}
        />
      )}
    </PracticeFrame>
  );
}
