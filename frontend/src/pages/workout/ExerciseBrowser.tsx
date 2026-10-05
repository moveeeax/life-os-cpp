import { useEffect, useState } from 'react';

import { useExercises, type ExerciseFilter } from '@/hooks/useWorkout';
import { EQUIPMENT, MUSCLES, capitalize } from '@/lib/workout';
import type { Exercise } from '@/lib/workout/types';

import { ExercisePhoto } from './ExercisePhoto';
import { LoadError } from './frame';
import { inputClass, secondaryButton } from './styles';

const SEARCH_DELAY_MS = 300;

interface ExerciseBrowserProps {
  /** Called with the exercise the user picked. */
  onSelect: (exercise: Exercise) => void;
}

/**
 * Search and filters over the library and the user's own exercises, as a
 * grid of cards. The Exercises page opens the card; the routine editor adds
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
      <div className="grid grid-cols-1 gap-3 sm:grid-cols-2 xl:grid-cols-4">
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
        <div className="grid grid-cols-2 gap-3 sm:grid-cols-3 xl:grid-cols-4" aria-label="Loading">
          {Array.from({ length: 8 }, (_, i) => (
            <div
              key={i}
              className="aspect-4/3 animate-pulse rounded-xl bg-gray-100 dark:bg-white/5"
            />
          ))}
        </div>
      ) : list.error ? (
        <LoadError error={list.error} onRetry={() => list.refetch()} />
      ) : rows.length === 0 ? (
        <p className="py-10 text-center text-theme-sm text-gray-500 dark:text-gray-400">
          No exercises match these filters.
        </p>
      ) : (
        <>
          <p className="text-theme-xs text-gray-500 dark:text-gray-400" aria-live="polite">
            {rows.length} of {total}
          </p>
          <ul className="grid grid-cols-2 gap-3 sm:grid-cols-3 xl:grid-cols-4">
            {rows.map((e) => (
              <li key={e.id}>
                <button
                  type="button"
                  onClick={() => onSelect(e)}
                  className="group flex h-full w-full flex-col overflow-hidden rounded-xl border border-gray-200 bg-white text-left transition hover:border-brand-300 focus-visible:ring-3 focus-visible:ring-brand-500/30 focus-visible:outline-hidden dark:border-gray-800 dark:bg-white/3 dark:hover:border-brand-800"
                >
                  <ExercisePhoto path={e.images[0]} alt={e.name} className="aspect-4/3 w-full" />
                  <span className="flex flex-1 flex-col gap-1 p-3">
                    <span className="text-theme-sm font-medium text-gray-800 dark:text-white/90">
                      {e.name}
                    </span>
                    <span className="text-theme-xs text-gray-500 dark:text-gray-400">
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
