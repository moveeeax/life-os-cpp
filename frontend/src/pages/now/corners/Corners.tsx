import { useEffect, useRef, useState } from 'react';

import { chips, type Chip, type Today } from '@/lib/now';

import { BodyPanel } from './BodyPanel';
import { DayPanel } from './DayPanel';
import { GoalsPanel } from './GoalsPanel';
import { Parts } from './Parts';
import { TasksPanel } from './TasksPanel';
import './corners.css';

const SPOT: Record<Chip['key'], string> = {
  body: 'c-tl',
  goals: 'c-tr',
  day: 'c-bl',
  tasks: 'c-br',
};

/**
 * The four corners of Now. A chip is one quiet line; a click opens its glass
 * panel over the picture. One panel at a time; Escape, a click outside or the
 * chip again closes it and focus returns to the chip.
 */
export function Corners({ today, hour, date }: { today: Today; hour: number; date: string }) {
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
  return (
    <>
      <div className="c-strip" data-open={open ? '' : undefined}>
        {list.map((c) => (
          <button
            key={c.key}
            ref={(el) => {
              chipRefs.current[c.key] = el;
            }}
            type="button"
            className={`c-chip ${SPOT[c.key]}`}
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
      {open && (
        <section
          ref={panelRef}
          id={`corner-${open}`}
          className={`c-panel ${SPOT[open]}`}
          role="dialog"
          tabIndex={-1}
          aria-label={list.find((c) => c.key === open)?.title}
        >
          {open === 'body' && <BodyPanel today={today} />}
          {open === 'goals' && <GoalsPanel today={today} date={date} />}
          {open === 'day' && <DayPanel today={today} />}
          {open === 'tasks' && <TasksPanel today={today} date={date} />}
        </section>
      )}
    </>
  );
}
