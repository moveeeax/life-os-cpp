/** The PUT body of a new set, as the log form built it inline (pinned by bodies.test.ts). */
import type { WorkoutSetInput } from './types';

export type SetValues = Pick<
  WorkoutSetInput,
  'weight_kg' | 'reps' | 'duration_seconds' | 'distance_m'
>;

export function newSetBody(i: {
  exerciseId: string;
  positions: number[];
  values: SetValues;
  warmup: boolean;
  now: string;
}) {
  return {
    session_exercise_id: i.exerciseId,
    position: i.positions.reduce((max, p) => Math.max(max, p), 0) + 1,
    kind: i.warmup ? ('warmup' as const) : ('work' as const),
    ...i.values,
    completed_at: i.now,
  };
}
