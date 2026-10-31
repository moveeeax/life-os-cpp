import { useQuery } from '@tanstack/react-query';
import { useEffect, useMemo, useRef, useState } from 'react';
import { useNavigate } from 'react-router';

import { useGoals } from '@/hooks/useGoals';
import { useHabits } from '@/hooks/useHabits';
import { useTrips } from '@/hooks/useTravel';
import { api } from '@/lib/api/client';
import { formatDay, todayLocal } from '@/lib/dates';
import {
  grouped,
  matches,
  noteHit,
  pageHits,
  PER_GROUP,
  taskHit,
  txHit,
  type Hit,
} from '@/lib/search/search';

import { useGlassNav } from '../useGlassNav';
import './search.css';

/** Dispatch this on window to open the search (the nav's button does). */
export const SEARCH_OPEN = 'life:search-open';

/** The palette waits this long after the last key before it asks the server. */
const DEBOUNCE_MS = 200;

/** One section that searches on the server; a section switched off answers 404 and shows nothing. */
function useRemote<T>(group: string, path: string, q: string, extra: Record<string, string> = {}) {
  return useQuery({
    queryKey: ['search', group, q],
    enabled: q.trim().length >= 2,
    retry: false,
    staleTime: 30_000,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: T[] }>(path, { query: { q, limit: PER_GROUP, ...extra }, signal }))
        .data,
  });
}

function Results({ q, onPick }: { q: string; onPick: (hit: Hit) => void }) {
  const today = todayLocal();
  const nav = useGlassNav().items;
  const tasks = useRemote<{ id: string; title: string; due?: string | null }>(
    'tasks',
    '/api/v1/tasks/items',
    q,
  );
  const notes = useRemote<{ id: string; title: string; date?: string | null; updated_at: string }>(
    'notes',
    '/api/v1/notes',
    q,
  );
  const money = useRemote<{
    id: string;
    name?: string | null;
    merchant?: string | null;
    date: string;
  }>('money', '/api/v1/money/transactions', q, { status: 'posted' });
  const goals = useGoals('active', today).data;
  const habits = useHabits().data;
  const trips = useTrips().data;

  const hits = useMemo<Hit[]>(() => {
    if (q.trim() === '') return [];
    return [
      ...pageHits(q, nav),
      ...(tasks.data ?? []).map((t) => taskHit(t, today)),
      ...(notes.data ?? []).map(noteHit),
      ...(money.data ?? []).map(txHit),
      ...(goals ?? [])
        .filter((g) => matches(g.title, q))
        .map((g) => ({
          key: `goal:${g.id}`,
          group: 'Goals' as const,
          title: g.title,
          when: g.due,
          to: '/goals',
        })),
      ...(habits ?? [])
        .filter((h) => matches(h.name, q))
        .map((h) => ({
          key: `habit:${h.id}`,
          group: 'Habits' as const,
          title: h.name,
          to: '/habits',
        })),
      ...(trips ?? [])
        .filter((t) => matches(t.name, q))
        .map((t) => ({
          key: `trip:${t.id}`,
          group: 'Trips' as const,
          title: t.name,
          when: t.start_date,
          to: `/travel/trips/${t.id}`,
        })),
    ];
  }, [q, nav, tasks.data, notes.data, money.data, goals, habits, trips, today]);

  const groups = grouped(hits);
  const flat = groups.flatMap(([, of]) => of);
  const [at, setAt] = useState(0);
  useEffect(() => setAt(0), [q]);
  const pick = useRef(onPick);
  pick.current = onPick;

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'ArrowDown') {
        e.preventDefault();
        setAt((i) => Math.min(i + 1, Math.max(flat.length - 1, 0)));
      } else if (e.key === 'ArrowUp') {
        e.preventDefault();
        setAt((i) => Math.max(i - 1, 0));
      } else if (e.key === 'Enter' && flat[at]) {
        e.preventDefault();
        pick.current(flat[at]);
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [flat, at]);

  const loading = tasks.isFetching || notes.isFetching || money.isFetching;
  if (q.trim() === '') {
    return <p className="s-empty">Tasks, notes, the ledger, goals, habits, trips and pages.</p>;
  }
  if (flat.length === 0) {
    return <p className="s-empty">{loading ? 'Searching…' : `Nothing for “${q.trim()}”`}</p>;
  }
  let index = -1;
  return (
    <div className="s-results" role="listbox" aria-label="Results">
      {groups.map(([group, of]) => (
        <div key={group} className="s-group" role="group" aria-label={group}>
          <div className="s-group-name">{group}</div>
          {of.map((h) => {
            index += 1;
            const i = index;
            return (
              <button
                key={h.key}
                type="button"
                role="option"
                aria-selected={i === at}
                className="s-hit"
                data-on={i === at ? '' : undefined}
                onMouseEnter={() => setAt(i)}
                onClick={() => onPick(h)}
              >
                <span className="s-hit-title">{h.title}</span>
                {h.when && (
                  <span className="s-hit-when" data-num-ok>
                    {formatDay(h.when, { yearUnless: today })}
                  </span>
                )}
              </button>
            );
          })}
        </div>
      ))}
    </div>
  );
}

/**
 * The search of the whole site: Cmd+K (Ctrl+K) or the nav's search button
 * opens it, arrows move, Enter opens the hit, Escape closes.
 */
export function SearchPalette() {
  const [open, setOpen] = useState(false);
  const [text, setText] = useState('');
  const [q, setQ] = useState('');
  const navigate = useNavigate();

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k') {
        e.preventDefault();
        setOpen((o) => !o);
      }
    };
    const onAsk = () => setOpen(true);
    window.addEventListener('keydown', onKey);
    window.addEventListener(SEARCH_OPEN, onAsk);
    return () => {
      window.removeEventListener('keydown', onKey);
      window.removeEventListener(SEARCH_OPEN, onAsk);
    };
  }, []);
  useEffect(() => {
    const t = window.setTimeout(() => setQ(text), DEBOUNCE_MS);
    return () => clearTimeout(t);
  }, [text]);
  useEffect(() => {
    if (!open) {
      setText('');
      setQ('');
    }
  }, [open]);

  if (!open) return null;
  return (
    <div className="s-veil" onMouseDown={(e) => e.target === e.currentTarget && setOpen(false)}>
      <div className="s-box" role="dialog" aria-label="Search">
        <input
          autoFocus
          role="combobox"
          aria-expanded="true"
          aria-label="Search Life OS"
          className="s-input"
          placeholder="Search tasks, notes, money, goals…"
          value={text}
          onChange={(e) => {
            setText(e.target.value);
            // A page name answers at once; the server sections after the pause.
            if (e.target.value.trim().length < 2) setQ(e.target.value);
          }}
          onKeyDown={(e) => {
            if (e.key === 'Escape') setOpen(false);
          }}
        />
        <Results
          q={q}
          onPick={(h) => {
            setOpen(false);
            void navigate(h.to);
          }}
        />
      </div>
    </div>
  );
}
