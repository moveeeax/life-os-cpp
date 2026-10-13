import { describe, expect, it } from 'vitest';

import { newSetBody } from './bodies';

describe('newSetBody', () => {
  it('puts the set after the last one, as the log form did', () => {
    expect(
      newSetBody({
        exerciseId: 'se1',
        positions: [1, 3, 2],
        values: { weight_kg: 80, reps: 8 },
        warmup: false,
        now: '2026-10-13T10:00:00.000Z',
      }),
    ).toEqual({
      session_exercise_id: 'se1',
      position: 4,
      kind: 'work',
      weight_kg: 80,
      reps: 8,
      completed_at: '2026-10-13T10:00:00.000Z',
    });
  });
  it('starts at one and marks a warm-up', () => {
    const b = newSetBody({
      exerciseId: 'se1',
      positions: [],
      values: { reps: 10 },
      warmup: true,
      now: 'x',
    });
    expect(b.position).toBe(1);
    expect(b.kind).toBe('warmup');
  });
});
