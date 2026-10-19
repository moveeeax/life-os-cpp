import { useState } from 'react';
import { Link } from 'react-router';

import { useMonths } from '@/hooks/usePractice';
import { money, monthLabel, netLine, todayLocal, type PracticeTotal } from '@/lib/practice';
import { Facts, Group, Num, Row } from '@/shell/kit';

import { LoadError, Placeholder, PracticeFrame, Tools } from './frame';

/** Income, expense and net of each currency, one fact line per currency. */
export function TotalsFacts({ totals }: { totals: readonly PracticeTotal[] }) {
  return (
    <Facts
      items={totals.map((t) => ({
        label: t.currency,
        value: (
          <Num>
            in {money(t.income, t.currency)} · out {money(t.expense, t.currency)} · net{' '}
            {money(t.net, t.currency, true)}
          </Num>
        ),
      }))}
    />
  );
}

/** The months of a year with their totals per currency; two currencies are never added. */
export function PracticeMonthsPage() {
  const thisYear = Number(todayLocal().slice(0, 4));
  const [year, setYear] = useState(thisYear);
  const months = useMonths(year);
  const list = months.data?.data ?? [];
  const totals = months.data?.totals ?? [];
  return (
    <PracticeFrame
      headline={<span data-num-ok>{year}</span>}
      sub={totals.length > 0 ? <Num>{netLine(totals)}</Num> : 'nothing recorded this year'}
      actions={
        <Tools>
          <span className="k-stepper">
            <button
              type="button"
              className="k-btn"
              aria-label="The year before"
              onClick={() => setYear(year - 1)}
            >
              ←
            </button>
            <button
              type="button"
              className="k-btn"
              aria-label="The year after"
              disabled={year >= thisYear}
              onClick={() => setYear(year + 1)}
            >
              →
            </button>
          </span>
        </Tools>
      }
    >
      {months.isPending ? (
        <Placeholder />
      ) : months.isError ? (
        <LoadError error={months.error} onRetry={() => void months.refetch()} />
      ) : list.length === 0 ? (
        <p className="k-empty">
          No incomes or expenses in <span data-num-ok>{year}</span>.
        </p>
      ) : (
        <>
          <Group label="Months">
            {list.map((m) => (
              <Row
                key={m.month}
                title={<span data-num-ok>{monthLabel(m.month)}</span>}
                sub={<Num>{netLine(m.totals)}</Num>}
              >
                <span className="k-muted">
                  <Num>{m.entries}</Num> {m.entries === 1 ? 'entry' : 'entries'}
                </span>
                <TotalsFacts totals={m.totals} />
                <span className="k-actions">
                  <Link to={`/practice/months/${m.month}`} className="k-link">
                    Open the month
                  </Link>
                </span>
              </Row>
            ))}
          </Group>
          <Group label="The year">
            <TotalsFacts totals={totals} />
          </Group>
        </>
      )}
    </PracticeFrame>
  );
}
