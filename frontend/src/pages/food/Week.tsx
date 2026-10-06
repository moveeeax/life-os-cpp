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
import { cn } from '@/lib/utils';

import { ChartCard } from '../health/ChartCard';
import { TimeChart } from '../health/TimeChart';
import { cardClass, iconButton, secondaryButton } from '../workout/styles';
import { FoodFrame } from './frame';

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

function WeekTable({ week }: { week: FoodWeek }) {
  const logged = week.days.filter((d) => d.entries > 0);
  const avg = (pick: (d: FoodWeek['days'][number]) => number) =>
    logged.length ? logged.reduce((n, d) => n + pick(d), 0) / logged.length : null;
  const cell = 'px-3 py-2 text-end tabular-nums';
  return (
    <div className={cn(cardClass, 'overflow-x-auto')}>
      <table className="w-full text-theme-sm">
        <thead>
          <tr className="text-theme-xs text-gray-500 uppercase">
            <th className="px-3 py-2 text-start font-medium">Day</th>
            <th className={cn(cell, 'font-medium')}>kcal</th>
            <th className={cn(cell, 'font-medium')}>P</th>
            <th className={cn(cell, 'font-medium')}>F</th>
            <th className={cn(cell, 'font-medium')}>C</th>
          </tr>
        </thead>
        <tbody className="divide-y divide-gray-100 dark:divide-white/5">
          {week.days.map((d) => (
            <tr key={d.date} className={d.entries === 0 ? 'text-gray-400' : undefined}>
              <td className="px-3 py-2">
                <Link to={`/food?date=${d.date}`} className="hover:underline">
                  {weekdayOf(d.date)}{' '}
                  <span className="text-theme-xs text-gray-500">{formatDate(d.date).slice(5)}</span>
                </Link>
              </td>
              <td className={cell}>{d.entries === 0 ? '–' : formatKcal(d.kcal)}</td>
              <td className={cell}>{d.entries === 0 ? '–' : formatGrams(d.protein_g)}</td>
              <td className={cell}>{d.entries === 0 ? '–' : formatGrams(d.fat_g)}</td>
              <td className={cell}>{d.entries === 0 ? '–' : formatGrams(d.carbs_g)}</td>
            </tr>
          ))}
        </tbody>
        <tfoot>
          <tr className="font-medium text-gray-800 dark:text-white/90">
            <td className="px-3 py-2">
              Average
              <span className="ms-1 text-theme-xs font-normal text-gray-500">
                {logged.length} {logged.length === 1 ? 'day' : 'days'}
              </span>
            </td>
            {(['kcal', 'protein_g', 'fat_g', 'carbs_g'] as const).map((k) => {
              const v = avg((d) => d[k]);
              return (
                <td key={k} className={cell}>
                  {v === null ? '–' : k === 'kcal' ? formatKcal(v) : formatGrams(v)}
                </td>
              );
            })}
          </tr>
          {week.targets && (
            <tr className="text-theme-xs text-gray-500">
              <td className="px-3 py-2">Goal</td>
              <td className={cell}>{formatKcal(week.targets.kcal)}</td>
              <td className={cell}>{formatGrams(week.targets.protein_g)}</td>
              <td className={cell}>{formatGrams(week.targets.fat_g)}</td>
              <td className={cell}>{formatGrams(week.targets.carbs_g)}</td>
            </tr>
          )}
        </tfoot>
      </table>
    </div>
  );
}

/** Seven days of calories against the goal. */
export function FoodWeekPage() {
  const [from, setFrom] = useWeekParam();
  const week = useFoodWeek(from);
  const thisWeek = mondayOf(todayLocal());
  const series = week.data ? weekSeries(week.data) : null;

  return (
    <FoodFrame
      title="Week"
      actions={
        <div className="flex flex-wrap items-center gap-2">
          <button
            type="button"
            onClick={() => setFrom(shiftDate(from, -7))}
            className={iconButton}
            aria-label="Previous week"
          >
            <ChevronLeft className="size-5" aria-hidden="true" />
          </button>
          <p className="min-w-36 text-center text-theme-sm font-medium text-gray-800 dark:text-white/90">
            {formatDate(from)} – {formatDate(shiftDate(from, 6))}
          </p>
          <button
            type="button"
            onClick={() => setFrom(shiftDate(from, 7))}
            className={iconButton}
            aria-label="Next week"
          >
            <ChevronRight className="size-5" aria-hidden="true" />
          </button>
          {from !== thisWeek && (
            <button
              type="button"
              onClick={() => setFrom(thisWeek)}
              className={cn(secondaryButton, 'px-3 py-2')}
            >
              This week
            </button>
          )}
        </div>
      }
    >
      <ChartCard
        title="Calories"
        hint={series?.target ? `Goal ${formatKcal(series.target)} kcal a day` : undefined}
        isPending={week.isPending}
        error={week.error}
        onRetry={() => void week.refetch()}
        isEmpty={!!week.data && week.data.days.every((d) => d.entries === 0)}
        emptyText="Nothing logged this week."
      >
        {series && (
          <TimeChart
            days={series.days}
            series={[
              { name: 'Eaten', type: 'column', data: series.kcal, color: '#465fff' },
              ...(series.target !== null
                ? [
                    {
                      name: 'Goal',
                      type: 'line' as const,
                      data: series.days.map(() => series.target),
                      color: '#f04438',
                    },
                  ]
                : []),
            ]}
            yTitles={['kcal']}
            yBounds={[{ min: 0 }]}
            format={(v) => `${formatKcal(v)} kcal`}
            height={260}
          />
        )}
      </ChartCard>
      {week.data && <WeekTable week={week.data} />}
    </FoodFrame>
  );
}
