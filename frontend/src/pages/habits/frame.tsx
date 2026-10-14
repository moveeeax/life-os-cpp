import { useState, type ReactNode } from 'react';

import { useErrorToast } from '@/hooks/useErrorToast';
import { useTickHabit } from '@/hooks/useHabits';
import { splitNumbers, type HabitDay, type HabitDays } from '@/lib/habits';
import { LoadError as KitLoadError, Num, SectionFrame, type TabItem } from '@/shell/kit';

import { HabitSheet } from './HabitSheet';
import './habits.css';

export { Placeholder } from '@/shell/kit';

const TABS: TabItem[] = [
  { to: '/habits', label: 'Today', end: true },
  { to: '/habits/week', label: 'Week' },
];

/** Frame of every Habits page on the calm sheet, with New habit in the tools. */
export function HabitsFrame({
  headline,
  sub,
  tools,
  children,
}: {
  headline: ReactNode;
  sub?: ReactNode;
  tools?: ReactNode;
  children: ReactNode;
}) {
  const [adding, setAdding] = useState(false);
  return (
    <SectionFrame
      eyebrow="Habits"
      headline={headline}
      sub={sub}
      tabs={TABS}
      tools={
        <>
          {tools}
          <span className="k-grow" />
          <button type="button" className="k-btn k-solid" onClick={() => setAdding(true)}>
            New habit
          </button>
        </>
      }
    >
      {children}
      {adding && <HabitSheet onClose={() => setAdding(false)} />}
    </SectionFrame>
  );
}

/** A failed query of Habits (a 404 names the module). */
export function LoadError({ error, onRetry }: { error: unknown; onRetry: () => void }) {
  return <KitLoadError error={error} onRetry={onRetry} module="habits" />;
}

/**
 * The ring that ticks a habit by hand on a day. A day ticked from data shows
 * filled and does not take a tick; a manual tick comes off on a second click.
 */
export function Tick({
  habit,
  date,
  day,
  className = 'h-tick',
}: {
  habit: Pick<HabitDays, 'id' | 'name'>;
  date: string;
  day?: HabitDay;
  className?: string;
}) {
  const tick = useTickHabit();
  useErrorToast(tick.error);
  const done = day?.done ?? false;
  const manual = day?.manual ?? false;
  const fromData = done && !manual;
  const pressed = tick.isPending ? !done : done;
  return (
    <button
      type="button"
      className={className}
      aria-pressed={pressed}
      aria-label={fromData ? `Done from data: ${habit.name}` : `Done: ${habit.name}`}
      title={fromData ? 'Ticked from data' : undefined}
      disabled={tick.isPending || fromData}
      onClick={() => tick.mutate({ id: habit.id, date, on: !manual })}
    />
  );
}

/** A line of words whose digits wear `.num` ("3 times a week"). */
export function Numbered({ text }: { text: string }) {
  return (
    <>
      {splitNumbers(text).map((p, i) =>
        p.num ? <Num key={i}>{p.text}</Num> : <span key={i}>{p.text}</span>,
      )}
    </>
  );
}
