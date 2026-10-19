import { entryTitle, money, shortDay, type PracticeEntry } from '@/lib/practice';
import { Facts, Num, Row } from '@/shell/kit';

/** One income or expense: what it was, the day, the signed amount; the links on click. */
export function EntryRow({
  entry,
  today,
  onEdit,
  onDelete,
}: {
  entry: PracticeEntry;
  today: string;
  onEdit: () => void;
  onDelete?: () => void;
}) {
  const income = entry.kind === 'income';
  return (
    <Row
      dot={income ? 'var(--g-brand)' : 'var(--g-ink-faint)'}
      title={entryTitle(entry)}
      sub={<span data-num-ok>{shortDay(entry.date, today)}</span>}
      right={
        <Num className={income ? 'k-pos' : undefined}>
          {money(income ? entry.amount : -entry.amount, entry.currency, true)}
        </Num>
      }
    >
      <Facts
        items={[
          { label: 'Kind', value: income ? 'Income' : 'Expense' },
          ...(entry.client_name ? [{ label: 'Client', value: entry.client_name }] : []),
          ...(entry.deal_title ? [{ label: 'Deal', value: entry.deal_title }] : []),
        ]}
      />
      <span className="k-actions">
        <button type="button" className="k-link" onClick={onEdit}>
          Edit
        </button>
        {onDelete && (
          <button type="button" className="k-link k-error-text" onClick={onDelete}>
            Delete
          </button>
        )}
      </span>
    </Row>
  );
}
