import { useEffect, useRef, useState } from 'react';

import {
  DEFAULT_CORNERS,
  chips,
  placeChips,
  type Chip,
  type NowCorners,
  type Today,
} from '@/lib/now';

import { BodyPanel } from './BodyPanel';
import { DayPanel } from './DayPanel';
import { GoalsPanel } from './GoalsPanel';
import { HabitsPanel } from './HabitsPanel';
import { Parts } from './Parts';
import { TasksPanel } from './TasksPanel';
import './corners.css';

/**
 * The corners of Now. A chip is one quiet line; a click opens its glass
 * panel over the picture. One panel at a time; Escape, a click outside or the
 * chip again closes it and focus returns to the chip.
 */
export function Corners({
  today,
  hour,
  date,
  corners = DEFAULT_CORNERS,
}: {
  today: Today;
  hour: number;
  date: string;
  corners?: NowCorners;
}) {
  const [open, setOpen] = useState<Chip['key'] | null>(null);
  const chipRefs = useRef<Partial<Record<Chip['key'], HTMLButtonElement | null>>>({});
  const panelRef = useRef<HTMLElement>(null);

  // An opened panel takes the focus, so the keyboard and a screen reader land in it.
  useEffect(() => {
    if (open) panelRef.current?.focus({ preventScroll: true });
  }, [open]);

  useEffect(() => {
    if (!open) return;
    const close = () => {
      setOpen(null);
      // After the repaint: on a phone the strip is hidden while the sheet is open.
      requestAnimationFrame(() => chipRefs.current[open]?.focus());
    };
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') close();
    };
    const onDown = (e: MouseEvent) => {
      const target = e.target as Node;
      if (panelRef.current?.contains(target)) return;
      if (Object.values(chipRefs.current).some((b) => b?.contains(target))) return;
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
  // The user's layout: a chip without a corner is not shown at all.
  const placed = placeChips(list, corners);
  const openSpot = placed.find((p) => p.chip.key === open)?.spot;
  return (
    <>
      <div className="c-strip" data-open={open ? '' : undefined}>
        {placed.map(({ chip: c, spot }) => (
          <button
            key={c.key}
            ref={(el) => {
              chipRefs.current[c.key] = el;
            }}
            type="button"
            className={`c-chip ${spot}`}
            aria-expanded={open === c.key}
            aria-controls={`corner-${c.key}`}
            onClick={() => setOpen((o) => (o === c.key ? null : c.key))}
          >
            <span className="c-k">
              {c.dot && (
                <span
                  className={c.dot === 'late' ? 'c-dot c-late' : 'c-dot'}
                  aria-label="needs a look"
                />
              )}
              {c.title}
            </span>
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
      {open && openSpot && (
        <section
          ref={panelRef}
          id={`corner-${open}`}
          className={`c-panel ${openSpot}`}
          role="dialog"
          tabIndex={-1}
          aria-label={list.find((c) => c.key === open)?.title}
        >
          {open === 'body' && <BodyPanel today={today} />}
          {open === 'goals' && <GoalsPanel today={today} date={date} />}
          {open === 'day' && <DayPanel today={today} date={date} hour={hour} />}
          {open === 'tasks' && <TasksPanel today={today} date={date} />}
          {open === 'habits' && <HabitsPanel date={date} />}
        </section>
      )}
    </>
  );
}
