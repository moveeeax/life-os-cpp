import { Link } from 'react-router';

import { countdowns, type Today } from '@/lib/now';

import { Meter } from './Parts';

export function GoalsPanel({ today, date }: { today: Today; date: string }) {
  const soon = countdowns(today, date);
  const goals = today.goals ?? [];
  return (
    <>
      <h2>
        Countdowns <Link to="/goals">Open Goals →</Link>
      </h2>
      {soon.length === 0 && <p className="c-empty">No goal is due in the next 60 days.</p>}
      {soon.map((c) => (
        <div key={c.id} className="c-count">
          <span>{c.title}</span>
          <span className="c-n num">{c.days}</span>
          <span className="c-d">
            {new Date(`${c.due}T00:00:00Z`).toLocaleDateString('en-GB', {
              day: 'numeric',
              month: 'short',
              timeZone: 'UTC',
            })}{' '}
            · {c.days === 0 ? 'today' : c.days === 1 ? 'tomorrow' : 'days left'}
          </span>
        </div>
      ))}
      {goals.length > 0 && (
        <>
          <h2>Pace</h2>
          <div>
            {goals.map((g) => (
              <Meter
                key={g.id}
                label={g.title}
                share={g.progress ?? 0}
                tone={g.pace === 'behind' ? 'warn' : 'ok'}
                value={
                  <span className={g.pace === 'behind' ? 'c-warn-text' : undefined}>
                    {g.pace === 'behind' ? 'behind' : 'on track'}
                  </span>
                }
              />
            ))}
          </div>
        </>
      )}
    </>
  );
}
