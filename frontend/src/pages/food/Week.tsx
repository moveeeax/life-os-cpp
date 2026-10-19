import { ChevronLeft, ChevronRight } from 'lucide-react';
import { Link, useSearchParams } from 'react-router';

import { useFoodWeek } from '@/hooks/useFood';
import {
  formatDate,
  formatGrams,
  formatKcal,
  mondayOf,
  parseDateParam,
  shiftDate,
  todayLocal,
  weekSeries,
  weekdayOf,
  type FoodWeek,
} from '@/lib/food';

import { TimeChart } from '../health/LazyTimeChart';
import { Facts, Group, Num, Row } from '@/shell/kit';
import { FoodFrame, LoadError, Placeholder } from './frame';

/** The Monday of the week lives in the URL; any other value means this week. */
function useWeekParam(): [string, (from: string) => void] {
  const [params, setParams] = useSearchParams();
  const thisWeek = mondayOf(todayLocal());
  const from = mondayOf(parseDateParam(params.get('from'), thisWeek));
  const set = (next: string) => {
    const p = new URLSearchParams(params);
    if (next === thisWeek) p.delete('from');
    else p.set('from', next);
    setParams(p, { replace: true });
  };
  return [from, set];
}

function WeekDays({ week }: { week: FoodWeek }) {
  const logged = week.days.filter((d) => d.entries > 0);
  const avg = (pick: (d: FoodWeek['days'][number]) => number) =>
    logged.length ? logged.reduce((n, d) => n + pick(d), 0) / logged.length : null;
  const kcal = avg((d) => d.kcal);
  const t = week.targets;
  return (
    <>
      <Facts
        items={[
          {
            label: `Average of ${logged.length} ${logged.length === 1 ? 'day' : 'days'}`,
            value: <Num>{kcal === null ? '–' : `${formatKcal(kcal)} kcal`}</Num>,
          },
          ...(t ? [{ label: 'Goal', value: <Num>{formatKcal(t.kcal)} kcal</Num> }] : []),
        ]}
      />
      <Group label="Days">
        {week.days.map((d) => (
          <Row
            key={d.date}
            title={
              <span className={d.entries === 0 ? 'k-muted' : undefined}>
                {weekdayOf(d.date)} {formatDate(d.date).slice(5)}
              </span>
            }
            right={
              d.entries === 0 ? (
                <span className="k-muted">–</span>
              ) : (
                <Num>{formatKcal(d.kcal)} kcal</Num>
              )
            }
          >
            {d.entries > 0 && (
              <span>
                P <Num>{formatGrams(d.protein_g)}</Num> · F <Num>{formatGrams(d.fat_g)}</Num> · C{' '}
                <Num>{formatGrams(d.carbs_g)}</Num>
                {t && (
                  <span className="k-muted">
                    {' '}
                    (goal <Num>{formatGrams(t.protein_g)}</Num> · <Num>{formatGrams(t.fat_g)}</Num>{' '}
                    · <Num>{formatGrams(t.carbs_g)}</Num>)
                  </span>
                )}
              </span>
            )}
            <span className="k-actions">
              <Link to={`/food?date=${d.date}`} className="k-link">
                Open the day
              </Link>
            </span>
          </Row>
        ))}
      </Group>
    </>
  );
}

/** Seven days of calories against the goal. */
export function FoodWeekPage() {
  const [from, setFrom] = useWeekParam();
  const week = useFoodWeek(from);
  const thisWeek = mondayOf(todayLocal());
  const series = week.data ? weekSeries(week.data) : null;
  const empty = !!week.data && week.data.days.every((d) => d.entries === 0);

  return (
    <FoodFrame
      title="Week"
      headline={`${formatDate(from)} – ${formatDate(shiftDate(from, 6))}`}
      sub={
        series?.target ? (
          <>
            Goal <Num>{formatKcal(series.target)}</Num> kcal a day
          </>
        ) : undefined
      }
      actions={
        <div className="k-stepper">
          <button
            type="button"
            onClick={() => setFrom(shiftDate(from, -7))}
            aria-label="Previous week"
          >
            <ChevronLeft className="size-4" aria-hidden="true" />
          </button>
          <span>Week</span>
          <button type="button" onClick={() => setFrom(shiftDate(from, 7))} aria-label="Next week">
            <ChevronRight className="size-4" aria-hidden="true" />
          </button>
          {from !== thisWeek && (
            <button type="button" className="k-link" onClick={() => setFrom(thisWeek)}>
              This week
            </button>
          )}
        </div>
      }
    >
      {week.isPending ? (
        <Placeholder />
      ) : week.isError ? (
        <LoadError error={week.error} onRetry={() => void week.refetch()} />
      ) : empty ? (
        <p className="k-empty">Nothing logged this week.</p>
      ) : (
        <>
          {series && (
            <Group label="Calories">
              <TimeChart
                days={series.days}
                series={[
                  {
                    name: 'Eaten',
                    type: 'column',
                    data: series.kcal,
                    color: 'rgba(255, 255, 255, 0.7)',
                  },
                  ...(series.target !== null
                    ? [
                        {
                          name: 'Goal',
                          type: 'line' as const,
                          data: series.days.map(() => series.target),
                          color: '#fca5a5',
                        },
                      ]
                    : []),
                ]}
                yTitles={['kcal']}
                yBounds={[{ min: 0 }]}
                format={(v) => `${formatKcal(v)} kcal`}
                xLabel={weekdayOf}
                height={240}
              />
            </Group>
          )}
          {week.data && <WeekDays week={week.data} />}
        </>
      )}
    </FoodFrame>
  );
}
