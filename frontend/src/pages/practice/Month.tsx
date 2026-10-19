import { useState } from 'react';
import { Link, useParams } from 'react-router';

import { useMonth } from '@/hooks/usePractice';
import { money, monthLabel, netLine, todayLocal, type PracticeEntry } from '@/lib/practice';
import { Group, Num, Row } from '@/shell/kit';

import { EntryRow } from './EntryRow';
import { LoadError, Placeholder, PracticeFrame, Tools } from './frame';
import { TotalsFacts } from './Months';
import { EntrySheet } from './Sheets';

/** One month: totals per currency, per client and currency, then every entry. */
export function PracticeMonthPage() {
  const { month = '' } = useParams();
  const today = todayLocal();
  const view = useMonth(month);
  const [editing, setEditing] = useState<PracticeEntry | null>(null);
  const valid = /^\d{4}-(0[1-9]|1[0-2])$/.test(month);
  const data = view.data;
  return (
    <PracticeFrame
      headline={valid ? <span data-num-ok>{monthLabel(month)}</span> : 'Month'}
      sub={data && data.totals.length > 0 ? <Num>{netLine(data.totals)}</Num> : undefined}
      actions={
        <Tools>
          <Link to="/practice/months" className="k-btn">
            All months
          </Link>
        </Tools>
      }
    >
      {view.isPending ? (
        <Placeholder />
      ) : view.isError || !data ? (
        <LoadError error={view.error} onRetry={() => void view.refetch()} notFound="Not a month." />
      ) : data.entries.length === 0 ? (
        <p className="k-empty">
          Nothing recorded in <span data-num-ok>{monthLabel(month)}</span>.
        </p>
      ) : (
        <>
          <Group label="Totals">
            <TotalsFacts totals={data.totals} />
          </Group>
          <Group label="By client">
            {data.by_client.map((c) => (
              <Row
                key={`${c.client_id ?? 'none'}-${c.currency}`}
                title={c.client_name ?? 'No client'}
                sub={c.currency}
                right={<Num>{money(c.net, c.currency, true)}</Num>}
              />
            ))}
          </Group>
          <Group label="Entries">
            {data.entries.map((e) => (
              <EntryRow key={e.id} entry={e} today={today} onEdit={() => setEditing(e)} />
            ))}
          </Group>
        </>
      )}
      {editing && <EntrySheet entry={editing} onClose={() => setEditing(null)} />}
    </PracticeFrame>
  );
}
