import { useState } from 'react';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { useConfirmTransactions, useCurrencies, useDeleteTransaction } from '@/hooks/useMoney';
import {
  formatDay,
  type Account,
  type Category,
  type InboxRow,
  type Transaction,
} from '@/lib/ledger';

import { Row as KitRow } from '@/shell/kit';
import { primaryButton } from '@/shell/kit/styles';
import { Amount } from './bits';

/** The rows a parse or an agent proposed: post them, edit one, or discard it. */
export function Inbox({
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
