import { useEffect, useState } from 'react';

import { useExercises, type ExerciseFilter } from '@/hooks/useWorkout';
import { EQUIPMENT, MUSCLES, capitalize } from '@/lib/workout';
import type { Exercise } from '@/lib/workout/types';

import { ExercisePhoto } from './ExercisePhoto';
import { LoadError } from './frame';
import { inputClass, secondaryButton } from '@/shell/kit/styles';

const SEARCH_DELAY_MS = 300;

interface ExerciseBrowserProps {
  /** Called with the exercise the user picked. */
  onSelect: (exercise: Exercise) => void;
}

/**
 * Search and filters over the library and the user's own exercises, as a
 * list of rows. The Exercises page opens the card; the routine editor adds
 * the exercise to the routine.
 */
export function ExerciseBrowser({ onSelect }: ExerciseBrowserProps) {
  const [text, setText] = useState('');
  const [filter, setFilter] = useState<ExerciseFilter>({
    q: '',
    muscle: '',
    equipment: '',
    source: '',
  });

  // The search runs after a pause in typing, not on every key.
  useEffect(() => {
    const q = text.trim();
    const timer = setTimeout(
      () => setFilter((f) => (f.q === q ? f : { ...f, q })),
      SEARCH_DELAY_MS,
    );
    return () => clearTimeout(timer);
  }, [text]);

  const list = useExercises(filter);
  const rows = list.data?.pages.flatMap((p) => p.data) ?? [];
  const total = list.data?.pages[0]?.total ?? 0;
  const set = (patch: Partial<ExerciseFilter>) => setFilter((f) => ({ ...f, ...patch }));

  return (
    <div className="flex flex-col gap-4">
      <div className="k-filters">
        <input
          type="search"
          value={text}
          onChange={(e) => setText(e.target.value)}
          placeholder="Search by name"
          aria-label="Search by name"
          maxLength={120}
          className={inputClass}
        />
        <select
          value={filter.muscle}
          onChange={(e) => set({ muscle: e.target.value })}
          aria-label="Muscle"
          className={inputClass}
        >
          <option value="">Any muscle</option>
          {MUSCLES.map((m) => (
            <option key={m} value={m}>
              {capitalize(m)}
            </option>
          ))}
        </select>
        <select
          value={filter.equipment}
          onChange={(e) => set({ equipment: e.target.value })}
          aria-label="Equipment"
          className={inputClass}
        >
          <option value="">Any equipment</option>
          {EQUIPMENT.map((m) => (
            <option key={m} value={m}>
              {capitalize(m)}
            </option>
          ))}
        </select>
        <select
          value={filter.source}
          onChange={(e) => set({ source: e.target.value })}
          aria-label="Source"
          className={inputClass}
        >
          <option value="">Library and my own</option>
          <option value="library">Library</option>
          <option value="custom">My own</option>
        </select>
      </div>

      {list.isPending ? (
        <div className="k-placeholder" aria-label="Loading" />
      ) : list.error ? (
        <LoadError error={list.error} onRetry={() => list.refetch()} />
      ) : rows.length === 0 ? (
        <p className="k-empty">No exercises match these filters.</p>
      ) : (
        <>
          <p className="k-sub" aria-live="polite">
            <span data-num-ok>
              {rows.length} of {total}
            </span>
          </p>
          <ul className="k-group-rows k-plain-list">
            {rows.map((e) => (
              <li key={e.id} className="k-row">
                <button type="button" onClick={() => onSelect(e)} className="k-row-head k-row-pic">
                  <ExercisePhoto path={e.images[0]} alt="" className="k-thumb" />
                  <span className="k-row-text">
                    <span className="k-row-title">{e.name}</span>
                    <span className="k-row-sub">
                      {[e.primary_muscles[0], e.equipment, e.source === 'custom' ? 'my own' : null]
                        .filter(Boolean)
                        .join(' · ')}
                    </span>
                  </span>
                </button>
              </li>
            ))}
          </ul>
          {list.hasNextPage && (
            <button
              type="button"
              onClick={() => list.fetchNextPage()}
              disabled={list.isFetchingNextPage}
              className={`${secondaryButton} self-center`}
            >
              {list.isFetchingNextPage ? 'Loading…' : 'Show more'}
            </button>
          )}
        </>
      )}
    </div>
  );
}
