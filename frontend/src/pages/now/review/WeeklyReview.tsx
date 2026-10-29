import { useQueryClient } from '@tanstack/react-query';
import { useEffect, useState } from 'react';
import { Link } from 'react-router';

import { Alert, AlertDescription } from '@/components/ui/alert';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { Today } from '@/lib/now/types';
import { Group, Row, Sheet } from '@/shell/kit';

import { tomorrowOf } from '../ani/evening';
import { closedKey, groupsOf, reviewText, weekOf } from './review';

function readClosed(week: string): string[] {
  try {
    const v = JSON.parse(localStorage.getItem(closedKey(week)) ?? '[]');
    return Array.isArray(v) ? v.map(String) : [];
  } catch {
    return [];
  }
}

function writeClosed(week: string, ids: string[]) {
  try {
    localStorage.setItem(closedKey(week), JSON.stringify(ids));
  } catch {
    // Private mode: the rows come back on the next open, that is all.
  }
}

/**
 * The weekly review: late tasks (to today, to tomorrow, done), goals behind
 * their pace (to the goal) and habits under their weekly target. A row
 * acted on or closed stays closed for the week.
 */
export function WeeklyReview({
  today,
  date,
  tz,
  onClose,
}: {
  today: Today | undefined;
  date: string;
  tz: string;
  onClose: () => void;
}) {
  const week = weekOf(date);
  const [closed, setClosed] = useState(() => readClosed(week));
  const close = (key: string) => {
    const next = [...closed, key];
    setClosed(next);
    writeClosed(week, next);
  };
  const g = groupsOf(today);
  const late = g.late.filter((t) => !closed.includes(`task:${t.id}`));
  const behind = g.behind.filter((x) => !closed.includes(`goal:${x.id}`));
  const habits = g.habits.filter((h) => !closed.includes(`habit:${h.name}`));
  const qc = useQueryClient();
  const [failed, setFailed] = useState(false);
  // On Sunday it also goes to Saved Messages when the owner turned that on;
  // the server keeps it to once a week and answers 404 without Telegram.
  const sunday = new Date(`${date}T12:00:00Z`).getUTCDay() === 0;
  useEffect(() => {
    if (!sunday || !today) return;
    void api
      .postJson('/api/v1/telegram/self', { body: { text: reviewText(today, week), week } })
      .catch(() => {});
    // Once per opening: the text of the moment it opened.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [sunday, week, today === undefined]);
  const patch = (id: string, body: Record<string, unknown>) =>
    api.patchJson(`/api/v1/tasks/items/${id}`, { body, query: { tz } }).then(
      () => {
        setFailed(false);
        close(`task:${id}`);
        void qc.invalidateQueries({ queryKey: qk.now.today(date, tz) });
      },
      () => setFailed(true),
    );
  const empty = late.length + behind.length + habits.length === 0;

  return (
    <Sheet title="Weekly review" onClose={onClose} width={520}>
      {failed ? (
        <Alert variant="destructive">
          <AlertDescription>Could not change the task; try again in Tasks.</AlertDescription>
        </Alert>
      ) : null}
      {empty ? <p className="text-sm text-muted-foreground">Nothing left for this week.</p> : null}
      {late.length > 0 && (
        <Group label="Late tasks" tone="late">
          {late.map((t) => (
            <Row
              key={t.id}
              title={<span className="num">{t.title}</span>}
              sub={<span data-num-ok>{t.due}</span>}
              action={
                <span className="flex gap-2">
                  <button
                    type="button"
                    aria-label={`Today: ${t.title}`}
                    onClick={() => void patch(t.id, { due: date })}
                  >
                    Today
                  </button>
                  <button
                    type="button"
                    aria-label={`Tomorrow: ${t.title}`}
                    onClick={() => void patch(t.id, { due: tomorrowOf(date) })}
                  >
                    Tomorrow
                  </button>
                  <button
                    type="button"
                    aria-label={`Done: ${t.title}`}
                    onClick={() => void patch(t.id, { status: 'done' })}
                  >
                    Done
                  </button>
                </span>
              }
            />
          ))}
        </Group>
      )}
      {behind.length > 0 && (
        <Group label="Goals behind pace">
          {behind.map((x) => (
            <Row
              key={x.id}
              title={<span className="num">{x.title}</span>}
              action={
                <span className="flex gap-2">
                  <Link to="/goals">Open</Link>
                  <button
                    type="button"
                    aria-label={`Close: ${x.title}`}
                    onClick={() => close(`goal:${x.id}`)}
                  >
                    ✓
                  </button>
                </span>
              }
            />
          ))}
        </Group>
      )}
      {habits.length > 0 && (
        <Group label="Habits under their week">
          {habits.map((h) => (
            <Row
              key={h.name}
              title={<span className="num">{h.name}</span>}
              sub={
                <span className="num">
                  {h.done} / {h.per_week}
                </span>
              }
              action={
                <button
                  type="button"
                  aria-label={`Close: ${h.name}`}
                  onClick={() => close(`habit:${h.name}`)}
                >
                  ✓
                </button>
              }
            />
          ))}
        </Group>
      )}
    </Sheet>
  );
}
