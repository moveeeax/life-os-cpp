import { Link } from 'react-router';

import { MONEY_ABOVE_USUAL, formatMoney, type Today } from '@/lib/now';

import { n0 } from './format';
import { Meter } from './Parts';

export function DayPanel({ today }: { today: Today }) {
  const f = today.food;
  const m = today.money;
  const above = (m?.currencies ?? []).filter(
    (c) =>
      c.projection != null &&
      c.median3 != null &&
      c.median3 > 0 &&
      c.projection >= c.median3 * MONEY_ABOVE_USUAL,
  );
  const macro = (label: string, key: 'protein_g' | 'fat_g' | 'carbs_g', warnWhenShort = false) => {
    const target = f?.target?.[key];
    if (!f || target == null) return null;
    const eaten = f.eaten[key];
    return (
      <Meter
        key={key}
        label={label}
        share={target > 0 ? eaten / target : 0}
        tone={warnWhenShort && eaten < target ? 'warn' : undefined}
        value={
          <span className="num">
            {n0.format(eaten)} / {n0.format(target)} g
          </span>
        }
      />
    );
  };
  return (
    <>
      <h2>
        Food today <Link to="/food">Open Food →</Link>
      </h2>
      {f ? (
        <>
          <div className="c-big">
            <span className="num">{n0.format(f.eaten.kcal)}</span>{' '}
            <small>
              {f.target ? (
                <>
                  of <span className="num">{n0.format(f.target.kcal)}</span> kcal
                </>
              ) : (
                'kcal'
              )}
            </small>
          </div>
          <div>
            {macro('Protein', 'protein_g', true)}
            {macro('Fat', 'fat_g')}
            {macro('Carbs', 'carbs_g')}
          </div>
        </>
      ) : (
        <p className="c-empty">Nothing eaten yet.</p>
      )}
      <div className="c-links">
        <Link className="c-link" to="/food">
          Describe a meal →
        </Link>
      </div>
      <h2>
        Money today <Link to="/money">Open Money →</Link>
      </h2>
      {(m?.spends_today ?? []).length === 0 && <p className="c-empty">Nothing spent today.</p>}
      <div>
        {(m?.spends_today ?? []).map((s) => (
          <div key={s.id} className="c-row c-plain">
            <span className="c-t">{s.name}</span>
            <span className="c-r num">
              −{formatMoney(s.amount)} {s.currency}
            </span>
          </div>
        ))}
        {above.map((c) => (
          <div key={c.currency} className="c-row c-plain">
            <span className="c-t c-warn-text">
              This month on pace for <span className="num">{n0.format(c.projection ?? 0)}</span>{' '}
              {c.currency}
            </span>
            <span className="c-r">
              usual <span className="num">{n0.format(c.median3 ?? 0)}</span>
            </span>
          </div>
        ))}
      </div>
      <div className="c-links">
        <Link className="c-link" to="/money">
          Add a spend →
        </Link>
      </div>
    </>
  );
}
