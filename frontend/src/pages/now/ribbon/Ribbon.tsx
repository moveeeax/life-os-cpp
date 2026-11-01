import { useQueryClient } from '@tanstack/react-query';
import { useEffect, useMemo, useRef, useState } from 'react';

import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';

import {
  RIBBON_FROM,
  RIBBON_TO,
  SOON_MINUTES,
  blocksOf,
  freeAt,
  nextEvent,
  nextWords,
  pctOf,
  pinsOf,
  type Block,
  RIBBON_POINT,
  hhmm,
  useNowDay,
} from './layout';
import { giveProps } from '@/lib/now/drag';
import './ribbon.css';

/**
 * The day ribbon under the clock: the calendar's events, free slots, what was
 * done (focus, workouts), timed tasks and now. A tap on a free slot offers a
 * focus until its end or a focus block in the calendar.
 */
export function Ribbon({
  date,
  tz,
  now,
  focusFrom = null,
  onFocus,
}: {
  date: string;
  tz: string;
  now: number;
  /** When the running focus started; null while none runs. */
  focusFrom?: number | null;
  onFocus: (minutes: number) => void;
}) {
  const day = useNowDay(date, tz);
  const blocks = useMemo(
    () => blocksOf(day.data, tz, now, focusFrom),
    [day.data, tz, now, focusFrom],
  );
  const pins = useMemo(() => pinsOf(day.data), [day.data]);
  const next = nextEvent(day.data, now);
  const nextLine = next
    ? nextWords(next, hhmm(Date.parse(day.data!.events.find((e) => e.id === next.id)!.start), tz))
    : null;
  const [lit, setLit] = useState<string | null>(null);
  const [slot, setSlot] = useState<Block | null>(null);
  const timer = useRef<number | undefined>(undefined);
  const qc = useQueryClient();
  // "Add a focus block" on its way: a second click adds no second event; a failure stays in the menu.
  const [adding, setAdding] = useState(false);
  const addingRef = useRef(false);
  const [addFailed, setAddFailed] = useState(false);
  const pick = (b: Block | null) => {
    setSlot(b);
    setAddFailed(false);
  };
  // On a narrow screen the ribbon scrolls sideways: it opens with now at a
  // third of the width, once, so the rest of the day is in view (14 px is
  // the ribbon's side padding around the track).
  const wrap = useRef<HTMLDivElement>(null);
  const placed = useRef(false);
  const hasDay = Boolean(day.data);
  useEffect(() => {
    const el = wrap.current;
    if (!hasDay || placed.current || !el) return;
    placed.current = true;
    if (el.scrollWidth <= el.clientWidth) return;
    const today = date === new Intl.DateTimeFormat('en-CA', { timeZone: tz }).format(new Date(now));
    if (!today) return;
    const hm = hhmm(now, tz);
    const pct = pctOf(Number(hm.slice(0, 2)) * 60 + Number(hm.slice(3)));
    if (pct <= 0 || pct >= 100) return;
    el.scrollLeft = Math.max(0, 14 + ((el.scrollWidth - 28) * pct) / 100 - el.clientWidth / 3);
  }, [hasDay, date, tz, now]);

  useEffect(() => {
    const onPoint = (e: Event) => {
      const id = (e as CustomEvent<string>).detail;
      setLit(id);
      window.clearTimeout(timer.current);
      timer.current = window.setTimeout(() => setLit(null), 6000);
    };
    window.addEventListener(RIBBON_POINT, onPoint);
    return () => {
      window.removeEventListener(RIBBON_POINT, onPoint);
      window.clearTimeout(timer.current);
    };
  }, []);

  if (!day.data) return null;
  const today = date === new Intl.DateTimeFormat('en-CA', { timeZone: tz }).format(new Date(now));
  const nowPct = today
    ? pctOf(Number(hhmm(now, tz).slice(0, 2)) * 60 + Number(hhmm(now, tz).slice(3)))
    : null;
  const hours = [];
  for (let h = RIBBON_FROM; h <= RIBBON_TO; h += 2) hours.push(h);
  const freeMinutes = blocks
    .filter((b) => b.kind === 'free')
    .reduce((s, b) => s + (b.end - b.start) / 60_000, 0);
  const calendarNote =
    day.data.calendar === 'disconnected'
      ? 'Google Calendar dropped the access: connect it again in Account'
      : day.data.calendar === 'not_linked'
        ? 'Connect Google Calendar in Account to see meetings here'
        : null;

  const addBlock = async (b: Block) => {
    if (addingRef.current) return;
    addingRef.current = true;
    setAdding(true);
    setAddFailed(false);
    const start = Math.max(b.start, now);
    const end = Math.min(b.end, start + 90 * 60_000);
    try {
      await api.postJson('/api/v1/calendar/events', {
        body: {
          title: 'Focus',
          start: new Date(start).toISOString(),
          end: new Date(end).toISOString(),
        },
      });
      setSlot(null);
      void qc.invalidateQueries({ queryKey: qk.now.day(date, tz) });
      void qc.invalidateQueries({ queryKey: qk.calendar() });
    } catch {
      setAddFailed(true);
    } finally {
      addingRef.current = false;
      setAdding(false);
    }
  };

  return (
    <div className="r-wrap" ref={wrap}>
      <div className="r-ribbon" role="group" aria-label="Your day">
        <div className="r-top">
          <span
            className={
              next && next.minutes <= SOON_MINUTES && next.minutes > 0 ? 'r-next r-soon' : 'r-next'
            }
          >
            {nextLine ?? calendarNote ?? 'Nothing else in the calendar today'}
          </span>
          {freeMinutes > 0 && (
            <span className="r-freetext num">
              free {Math.floor(freeMinutes / 60)} h {Math.round(freeMinutes % 60)} min
            </span>
          )}
        </div>
        <div
          className="r-track"
          onClick={(e) => {
            const r = e.currentTarget.getBoundingClientRect();
            const b = freeAt(blocks, ((e.clientX - r.left) / r.width) * 100);
            if (b && b.end > now) pick(b);
          }}
        >
          {blocks.map((b) => (
            <div
              key={b.key}
              className={`r-b r-${b.kind}${b.id && b.id === lit ? ' r-lit' : ''}`}
              style={{ left: `${b.left}%`, width: `${b.width}%` }}
              title={`${b.title}, ${hhmm(b.start, tz)}–${hhmm(b.end, tz)}`}
              data-event={b.id}
              {...(b.kind === 'meet' && b.id
                ? giveProps({ kind: 'event', id: b.id, title: b.title })
                : {})}
            >
              {b.kind !== 'free' && b.width > 4.5 ? b.title : ''}
            </div>
          ))}
          {pins.map((p) => (
            <div
              key={p.key}
              className={p.done ? 'r-pin r-pin-done' : 'r-pin'}
              style={{ left: `${p.left}%` }}
              title={p.title}
            >
              <span>{p.title}</span>
            </div>
          ))}
          {nowPct !== null && nowPct > 0 && nowPct < 100 && (
            <div
              className="r-now"
              style={{ left: `${nowPct}%` }}
              aria-label={`Now ${hhmm(now, tz)}`}
            />
          )}
        </div>
        <div className="r-hours" aria-hidden="true">
          {hours.map((h) => (
            <span key={h} style={{ left: `${pctOf(h * 60)}%` }}>
              {String(h).padStart(2, '0')}
            </span>
          ))}
        </div>
        {slot && (
          <div className="r-menu" role="dialog" aria-label="Free time">
            <span>
              Free {hhmm(Math.max(slot.start, now), tz)}–{hhmm(slot.end, tz)}
            </span>
            <button
              type="button"
              onClick={() => {
                onFocus(
                  Math.max(
                    5,
                    Math.min(90, Math.round((slot.end - Math.max(slot.start, now)) / 60_000)),
                  ),
                );
                pick(null);
              }}
            >
              Focus until {hhmm(Math.min(slot.end, Math.max(slot.start, now) + 90 * 60_000), tz)}
            </button>
            {day.data.calendar === 'ok' && (
              <button type="button" disabled={adding} onClick={() => void addBlock(slot)}>
                {adding ? 'Adding…' : 'Add a focus block'}
              </button>
            )}
            {addFailed && (
              <span className="r-menu-error" role="alert">
                The block was not added. Try again.
              </span>
            )}
            <button type="button" onClick={() => pick(null)}>
              Close
            </button>
          </div>
        )}
      </div>
    </div>
  );
}
