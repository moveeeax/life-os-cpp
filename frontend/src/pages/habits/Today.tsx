import { useState } from 'react';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import {
  useArchiveHabit,
  useDeleteHabit,
  useHabitDays,
  useHabits,
  habitsZone,
} from '@/hooks/useHabits';
import { useErrorToast } from '@/hooks/useErrorToast';
import { useMe } from '@/hooks/useMe';
import { useNowSettings } from '@/hooks/useNow';
import {
  addDays,
  dayCount,
  dayOf,
  formatValue,
  goalLine,
  hasTarget,
  lastDays,
  longDay,
  progressLine,
  scheduleWords,
  subline,
  targetLine,
  type Habit,
  type HabitDays,
} from '@/lib/habits';
import { cleanCorners, isPlaced, localDate, withChip } from '@/lib/now';
import { ArrangeSheet } from '@/shell/ArrangeSheet';
import { useGlassNav } from '@/shell/useGlassNav';
import { Banner, Group, Num, Row } from '@/shell/kit';

import { AddSheet } from './AddSheet';
import { HabitSheet } from './HabitSheet';
import { MoodToday } from './Mood';
import { HabitsFrame, LoadError, Numbered, Placeholder, Tick } from './frame';

/** The value on the right of a row: the source's value today, or the week's count. */
function RightOf({ h, today }: { h: HabitDays; today: string }) {
  if (h.no_data) return <span className="k-muted">no data</span>;
  if (h.schedule === 'weekly') {
    return (
      <span>
        {h.source !== 'manual' && <span className="h-auto">auto · </span>}
        <Num>{h.week_done ?? 0}</Num> of <Num>{h.per_week ?? 1}</Num>
      </span>
    );
  }
  const d = dayOf(h, today);
  if (hasTarget(h)) return <Num>{progressLine(h, d?.value)}</Num>;
  if (h.source === 'manual' || d?.value == null) return null;
  return (
    <span>
      <span className="h-auto">auto · </span>
      <Num>{formatValue(h, d.value)}</Num>
    </span>
  );
}

function Streak({ h }: { h: HabitDays }) {
  const best =
    h.best > h.streak ? (
      <>
        , the best was <Num>{h.best}</Num>
      </>
    ) : null;
  if (h.streak <= 0) {
    return (
      <span>
        No streak yet
        {h.best > 0 && (
          <>
            ; the best was <Num>{h.best}</Num>
          </>
        )}
        .
      </span>
    );
  }
  if (h.schedule === 'daily') {
    return (
      <span>
        <Num>{h.streak}</Num> {h.streak === 1 ? 'day' : 'days'} in a row{best}.
      </span>
    );
  }
  return (
    <span>
      Met the goal <Num>{h.streak}</Num> {h.streak === 1 ? 'week' : 'weeks'} in a row{best}.
    </span>
  );
}

/** The last 30 days as dots, oldest first. */
function Strip({ h, today }: { h: HabitDays; today: string }) {
  const days = lastDays(h, today);
  return (
    <span className="h-strip-wrap">
      <span className="k-muted">Last 30 days</span>
      <span className="h-strip" aria-label="The last 30 days, oldest first">
        {days.map((on, i) => (
          <i key={i} className={on ? 'h-on' : undefined} />
        ))}
      </span>
    </span>
  );
}

function HabitRow({
  h,
  today,
  onEdit,
  onArchive,
}: {
  h: HabitDays;
  today: string;
  onEdit: () => void;
  onArchive: () => void;
}) {
  const goal = goalLine(h);
  const [adding, setAdding] = useState(false);
  const d = dayOf(h, today);
  const target = hasTarget(h);
  return (
    <Row
      title={<span data-num-ok>{h.name}</span>}
      sub={<Numbered text={subline(h)} />}
      right={<RightOf h={h} today={today} />}
      action={
        target ? (
          <>
            <button
              type="button"
              className="h-tick h-add"
              aria-pressed={d?.done ?? false}
              aria-label={`Add to ${h.name}`}
              onClick={() => setAdding(true)}
            />
            {adding && (
              <AddSheet habit={h} date={today} value={d?.value} onClose={() => setAdding(false)} />
            )}
          </>
        ) : (
          <Tick habit={h} date={today} day={d} />
        )
      }
    >
      <Streak h={h} />
      {target && (
        <span>
          A day counts at <Num>{targetLine(h)}</Num>
        </span>
      )}
      {goal && (
        <span>
          Goal <Num>{goal}</Num>
          {h.source === 'sleep' ? ' of sleep' : ''}, <Numbered text={scheduleWords(h)} />
        </span>
      )}
      <Strip h={h} today={today} />
      <span className="k-actions">
        <button type="button" className="k-link" onClick={onEdit}>
          Edit
        </button>
        <button type="button" className="k-link" onClick={onArchive}>
          Archive
        </button>
      </span>
    </Row>
  );
}

function Archived() {
  const list = useHabits(true);
  const restore = useArchiveHabit();
  const [deleting, setDeleting] = useState<Habit | null>(null);
  const remove = useDeleteHabit(() => setDeleting(null));
  useErrorToast(restore.error ?? remove.error);
  const rows = list.data ?? [];
  if (list.isPending) return <Placeholder />;
  if (rows.length === 0) return <p className="k-empty">Nothing archived.</p>;
  return (
    <Group label="Archived">
      {rows.map((h) => (
        <Row
          key={h.id}
          title={<span data-num-ok>{h.name}</span>}
          sub={<Numbered text={subline(h)} />}
        >
          <span className="k-actions">
            <button
              type="button"
              className="k-link"
              onClick={() => restore.mutate({ id: h.id, archived: false })}
            >
              Restore
            </button>
            <button type="button" className="k-link k-error-text" onClick={() => setDeleting(h)}>
              Delete
            </button>
          </span>
        </Row>
      ))}
      {deleting && (
        <ConfirmDialog
          title={`Delete ${deleting.name}?`}
          description="Its ticks go with it. This cannot be undone."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate(deleting.id)}
          onClose={() => setDeleting(null)}
        />
      )}
    </Group>
  );
}

/** "Habits is not on Now yet": opens Arrange with the plate placed. */
function ShowOnNow() {
  const me = useMe().data ?? null;
  const nav = useGlassNav();
  const settings = useNowSettings(nav.nowOn === false ? null : (me?.id ?? null));
  const [arranging, setArranging] = useState(false);
  if (!settings.data || isPlaced(cleanCorners(settings.data.corners), 'habits')) return null;
  const placed = withChip(cleanCorners(settings.data.corners), 'habits');
  return (
    <>
      <Banner action="Show on Now" onAction={() => setArranging(true)}>
        Habits is not on Now yet
      </Banner>
      {arranging && (
        <ArrangeSheet
          settings={settings.data}
          initialCorners={placed}
          onClose={() => setArranging(false)}
        />
      )}
    </>
  );
}

/** Today's habits: every day, then this week; a row opens to its streak and last 30 days. */
export function HabitsTodayPage() {
  const today = localDate(new Date(), habitsZone());
  const q = useHabitDays(addDays(today, -29), today);
  const archive = useArchiveHabit();
  useErrorToast(archive.error);
  const [editing, setEditing] = useState<Habit | null>(null);
  const [showArchived, setShowArchived] = useState(false);
  const list = q.data?.data ?? [];
  const day = q.data?.today ?? today;
  const count = dayCount(list, day);
  const daily = list.filter((h) => h.schedule === 'daily');
  const weekly = list.filter((h) => h.schedule === 'weekly');
  return (
    <HabitsFrame
      headline={
        list.length === 0 ? (
          'Habits'
        ) : (
          <>
            <Num>{count.done}</Num> of <Num>{count.total}</Num> today
          </>
        )
      }
      sub={
        <>
          {longDay(day)}
          {list.length > 0 &&
            (count.left > 0 ? (
              <>
                {' · '}
                <Num>{count.left}</Num> left
              </>
            ) : (
              ' · every day done'
            ))}
        </>
      }
    >
      <ShowOnNow />
      <MoodToday today={day} />
      {q.isPending ? (
        <Placeholder />
      ) : q.isError ? (
        <LoadError error={q.error} onRetry={() => void q.refetch()} />
      ) : list.length === 0 ? (
        <p className="k-empty">
          No habits yet. Add one by hand, or one that Mi or a Training session ticks.
        </p>
      ) : (
        <>
          {daily.length > 0 && (
            <Group label="Every day">
              {daily.map((h) => (
                <HabitRow
                  key={h.id}
                  h={h}
                  today={day}
                  onEdit={() => setEditing(h)}
                  onArchive={() => archive.mutate({ id: h.id, archived: true })}
                />
              ))}
            </Group>
          )}
          {weekly.length > 0 && (
            <Group label="This week">
              {weekly.map((h) => (
                <HabitRow
                  key={h.id}
                  h={h}
                  today={day}
                  onEdit={() => setEditing(h)}
                  onArchive={() => archive.mutate({ id: h.id, archived: true })}
                />
              ))}
            </Group>
          )}
        </>
      )}
      <p className="h-foot">
        <button type="button" className="k-link" onClick={() => setShowArchived((v) => !v)}>
          {showArchived ? 'Hide archived' : 'Archived habits'}
        </button>
      </p>
      {showArchived && <Archived />}
      {editing && <HabitSheet habit={editing} onClose={() => setEditing(null)} />}
    </HabitsFrame>
  );
}
