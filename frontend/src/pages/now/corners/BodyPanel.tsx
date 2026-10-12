import { Link } from 'react-router';

import { formatSleep, type Today } from '@/lib/now';

import { n0, n1 } from './format';

const STAGE: Record<string, { label: string; color: string }> = {
  deep: { label: 'Deep', color: '#818cf8' },
  light: { label: 'Light', color: '#a5b4fc' },
  rem: { label: 'REM', color: '#c4b5fd' },
  awake: { label: 'Awake', color: '#fca5a5' },
};

function clock(iso: string | null | undefined): string {
  if (!iso) return '';
  return new Date(iso).toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit' });
}

export function BodyPanel({ today }: { today: Today }) {
  const h = today.health;
  const sleep = h?.sleep ?? null;
  const total = sleep ? sleep.stages.reduce((a, s) => a + s.minutes, 0) : 0;
  return (
    <>
      <h2>
        Body <Link to="/health">Open Body →</Link>
      </h2>
      {!h && <p className="c-empty">No data from the band today. Link it in Body.</p>}
      {sleep && (
        <>
          <div className="c-big">
            <span className="num">{formatSleep(sleep.minutes)}</span>{' '}
            <small>
              asleep{sleep.start ? `, ${clock(sleep.start)} – ${clock(sleep.end)}` : ''}
            </small>
          </div>
          {total > 0 && (
            <>
              <div className="c-stages" aria-hidden="true">
                {sleep.stages.map((s, i) => (
                  <span
                    key={i}
                    style={{
                      width: `${(s.minutes / total) * 100}%`,
                      background: STAGE[s.stage]?.color ?? '#cbd5e1',
                    }}
                  />
                ))}
              </div>
              <div className="c-legend">
                {Object.entries(
                  sleep.stages.reduce<Record<string, number>>(
                    (a, s) => ({ ...a, [s.stage]: (a[s.stage] ?? 0) + s.minutes }),
                    {},
                  ),
                ).map(([stage, minutes]) => (
                  <span key={stage}>
                    <i style={{ background: STAGE[stage]?.color ?? '#cbd5e1' }} />
                    {STAGE[stage]?.label ?? stage}{' '}
                    <span className="num">{formatSleep(minutes)}</span>
                  </span>
                ))}
              </div>
            </>
          )}
        </>
      )}
      {h && (
        <div>
          {h.steps != null && (
            <div className="c-row c-plain">
              <span className="c-t">Steps</span>
              <span className="c-r num">{n0.format(h.steps)}</span>
            </div>
          )}
          {h.active_kcal != null && (
            <div className="c-row c-plain">
              <span className="c-t">Active</span>
              <span className="c-r num">{n0.format(h.active_kcal)} kcal</span>
            </div>
          )}
          {h.weight_kg != null && (
            <div className="c-row c-plain">
              <span className="c-t">Weight</span>
              <span className="c-r num">
                {n1.format(h.weight_kg)} kg
                {h.weight_change_7d != null &&
                  ` · ${h.weight_change_7d > 0 ? '+' : h.weight_change_7d < 0 ? '−' : '±'}${n1.format(Math.abs(h.weight_change_7d))} in a week`}
              </span>
            </div>
          )}
          {h.resting_bpm != null && (
            <div className="c-row c-plain">
              <span className="c-t">Resting heart rate</span>
              <span className="c-r num">{h.resting_bpm} bpm</span>
            </div>
          )}
        </div>
      )}
    </>
  );
}
