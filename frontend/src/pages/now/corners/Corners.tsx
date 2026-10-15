import { useEffect, useRef, useState } from 'react';

import {
  DEFAULT_CORNERS,
  chips,
  dockChips,
  type Chip,
  type NowCorners,
  type NowLink,
  type Today,
} from '@/lib/now';

import { AddPanel } from './AddPanel';
import { BodyPanel } from './BodyPanel';
import { DayPanel } from './DayPanel';
import { GoalsPanel } from './GoalsPanel';
import { HabitsPanel } from './HabitsPanel';
import { LinksPanel } from './LinksPanel';
import { Parts } from './Parts';
import { TasksPanel } from './TasksPanel';
import './corners.css';

/** What a panel shows: a chip of the dock, or one of the action corners. */
type PanelId = Chip['key'] | 'add' | 'links';

/** Where a panel sits: its corner, or above the dock. */
function spotOf(id: PanelId): string {
  if (id === 'add') return 'c-tl';
  if (id === 'links') return 'c-bl';
  if (id === 'tasks') return 'c-br';
  return 'c-dock';
}

function PlusIcon() {
  return (
    <svg
      aria-hidden="true"
      width="16"
      height="16"
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
    >
      <path d="M12 5v14M5 12h14" />
    </svg>
  );
}

function LinkIcon() {
  return (
    <svg
      aria-hidden="true"
      width="16"
      height="16"
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
    >
      <path d="M10 13a5 5 0 0 0 7.5.5l3-3a5 5 0 0 0-7-7l-1.7 1.7" />
      <path d="M14 11a5 5 0 0 0-7.5-.5l-3 3a5 5 0 0 0 7 7l1.7-1.7" />
    </svg>
  );
}

/**
 * The dock and the corners of Now. The dock is one glass strip at the bottom
 * with a tile per placed chip (Body, Goals, Food and money, Habits); the
 * corners hold the actions: Add top left, Links bottom left, Tasks bottom
 * right (the weather, top right, is drawn by Now itself). A click opens one
 * glass panel over the picture, one at a time; Escape, a click outside or the
 * opener again closes it and the focus returns to the opener.
 */
export function Corners({
  today,
  hour,
  date,
  corners = DEFAULT_CORNERS,
  links,
}: {
  today: Today;
  hour: number;
  date: string;
  corners?: NowCorners;
  links: NowLink[];
}) {
  const [open, setOpen] = useState<PanelId | null>(null);
  const openerRefs = useRef<Partial<Record<PanelId, HTMLButtonElement | null>>>({});
  const panelRef = useRef<HTMLElement>(null);

  // An opened panel takes the focus, so the keyboard and a screen reader land in it.
  useEffect(() => {
    if (open) panelRef.current?.focus({ preventScroll: true });
  }, [open]);

  useEffect(() => {
    if (!open) return;
    const close = () => {
      setOpen(null);
      // After the repaint: on a phone the openers are hidden while the sheet is open.
      requestAnimationFrame(() => openerRefs.current[open]?.focus());
    };
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') close();
    };
    const onDown = (e: MouseEvent) => {
      const target = e.target as Node;
      if (panelRef.current?.contains(target)) return;
      if (Object.values(openerRefs.current).some((b) => b?.contains(target))) return;
      setOpen(null);
    };
    document.addEventListener('keydown', onKey);
    document.addEventListener('mousedown', onDown);
    return () => {
      document.removeEventListener('keydown', onKey);
      document.removeEventListener('mousedown', onDown);
    };
  }, [open]);

  const list = chips(today, hour, date);
  const tasks = list.find((c) => c.key === 'tasks');
  const tasksEmpty = (today.tasks?.today ?? []).length === 0 && !(today.tasks?.late_count ?? 0);
  const toggle = (id: PanelId) => setOpen((o) => (o === id ? null : id));
  const opener = (id: PanelId) => ({
    ref: (el: HTMLButtonElement | null) => {
      openerRefs.current[id] = el;
    },
    'aria-expanded': open === id,
    'aria-controls': `corner-${id}`,
    onClick: () => toggle(id),
  });
  const title: Record<PanelId, string> = {
    add: 'Add',
    links: 'Links',
    body: 'Body',
    goals: 'Goals',
    day: 'Food and money',
    tasks: 'Tasks',
    habits: 'Habits',
  };

  return (
    <>
      <div className="n-corners" data-open={open ? '' : undefined}>
        <button type="button" className="n-corner n-tl" {...opener('add')}>
          <PlusIcon />
          Add
        </button>
        <button type="button" className="n-corner n-bl" {...opener('links')}>
          <LinkIcon />
          Links
        </button>
        <button type="button" className="n-corner n-br" {...opener('tasks')}>
          {tasks?.dot && (
            <span
              className={tasks.dot === 'late' ? 'c-dot c-late' : 'c-dot'}
              aria-label="needs a look"
            />
          )}{' '}
          Tasks{' '}
          {tasks && !tasksEmpty && (
            <span className="n-corner-s">
              <Parts parts={tasks.main} />
            </span>
          )}
        </button>
      </div>
      <div className="n-dock" data-open={open ? '' : undefined}>
        {dockChips(list, corners).map((c) => (
          <button key={c.key} type="button" className="n-tile" {...opener(c.key)}>
            <span className="c-k">
              {c.dot && (
                <span
                  className={c.dot === 'late' ? 'c-dot c-late' : 'c-dot'}
                  aria-label="needs a look"
                />
              )}{' '}
              {c.title}
            </span>{' '}
            <span className="c-v">
              <Parts parts={c.main} />
            </span>
            {c.small.length > 0 && (
              <span className="c-s">
                <Parts parts={c.small} />
              </span>
            )}
          </button>
        ))}
      </div>
      {open && (
        <section
          ref={panelRef}
          id={`corner-${open}`}
          className={`c-panel ${spotOf(open)}`}
          role="dialog"
          tabIndex={-1}
          aria-label={title[open]}
        >
          {open === 'add' && <AddPanel date={date} hour={hour} />}
          {open === 'links' && <LinksPanel links={links} />}
          {open === 'body' && <BodyPanel today={today} />}
          {open === 'goals' && <GoalsPanel today={today} date={date} />}
          {open === 'day' && <DayPanel today={today} date={date} />}
          {open === 'tasks' && <TasksPanel today={today} date={date} />}
          {open === 'habits' && <HabitsPanel date={date} />}
        </section>
      )}
    </>
  );
}
