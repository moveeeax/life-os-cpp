import { Link } from 'react-router';

import { countdownFigure, countdowns, goalPace, type Today } from '@/lib/now';

import { Meter } from './Parts';

export function GoalsPanel({ today, date }: { today: Today; date: string }) {
  const soon = countdowns(today, date);
  const goals = today.goals ?? [];
  const near = today.travel?.near ?? [];
  return (
    <>
      {near.length > 0 && (
        <>
          <h2>
            Days abroad <Link to="/travel/days">Open Travel →</Link>
          </h2>
          {near.map((n, i) => (
            <div key={`${i}-${n.rule}`} className="c-count">
              <span>{n.rule}</span>
              <span className="c-n num c-warn-text">{n.left}</span>
              <span className="c-d">days left</span>
            </div>
          ))}
        </>
      )}
      <h2>
        Countdowns <Link to="/goals">Open Goals →</Link>
      </h2>
      {soon.length === 0 && <p className="c-empty">No goal or trip in the next 60 days.</p>}
      {soon.map((c) => {
        const f = countdownFigure(c.days);
        return (
          <div key={c.id} className="c-count">
            <span data-num-ok>{c.title}</span>
            {f.figure !== null && <span className="c-n num">{f.figure}</span>}
            <span className="c-d">
              {new Date(`${c.due}T00:00:00Z`).toLocaleDateString('en-GB', {
                day: 'numeric',
                month: 'short',
                timeZone: 'UTC',
              })}{' '}
              · {f.figure === null ? c.words : f.words}
            </span>
          </div>
        );
      })}
      {goals.length > 0 && (
        <>
          <h2>Pace</h2>
          <div>
            {goals.map((g) => {
              const pace = goalPace(g);
              const off = pace.tone !== 'ok';
              return (
                <Meter
                  key={g.id}
                  label={g.title}
                  share={g.progress ?? 0}
                  tone={off ? 'warn' : 'ok'}
                  value={<span className={off ? 'num c-warn-text' : 'num'}>{pace.text}</span>}
                />
              );
            })}
          </div>
        </>
      )}
    </>
  );
}
