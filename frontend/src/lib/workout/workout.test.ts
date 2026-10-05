import { describe, expect, it } from 'vitest';

import {
  draftFromRoutine,
  emptyDraft,
  formatSeconds,
  itemFromExercise,
  linesOf,
  mediaUrl,
  moveItem,
  parseField,
  targetSummary,
  toRoutineInput,
  validateDraft,
  weekdayLabel,
  type DraftItem,
} from './index';
import type { Exercise, Routine } from './types';

const squat: Pick<Exercise, 'id' | 'name' | 'tracking_mode' | 'images'> = {
  id: 'Barbell_Squat',
  name: 'Barbell Squat',
  tracking_mode: 'weight_reps',
  images: [],
};
const plank: typeof squat = { id: 'Plank', name: 'Plank', tracking_mode: 'duration', images: [] };

const item = (over: Partial<DraftItem> = {}): DraftItem => ({
  ...itemFromExercise(squat, 'k1'),
  ...over,
});

describe('mediaUrl', () => {
  it('keeps the folder and escapes each segment', () => {
    expect(mediaUrl('Barbell_Squat/0.jpg')).toBe('/exercise-media/Barbell_Squat/0.jpg');
    expect(mediaUrl('A #1/0.jpg')).toBe('/exercise-media/A%20%231/0.jpg');
  });
});

describe('weekdayLabel', () => {
  it('names 1..7 from Monday and falls back for the rest', () => {
    expect(weekdayLabel(1)).toBe('Monday');
    expect(weekdayLabel(7)).toBe('Sunday');
    expect(weekdayLabel(null)).toBe('Any day');
    expect(weekdayLabel(8)).toBe('Any day');
  });
});

describe('parseField', () => {
  it('reads an empty field as null and a whole number in range as itself', () => {
    expect(parseField('', 1, 50)).toBeNull();
    expect(parseField('  ', 1, 50)).toBeNull();
    expect(parseField(' 12 ', 1, 50)).toBe(12);
    expect(parseField('0', 0, 3600)).toBe(0);
  });

  it('rejects fractions, signs, text and out-of-range numbers', () => {
    for (const bad of ['1.5', '-1', 'abc', '1e3', '51', '0']) {
      expect(parseField(bad, 1, 50), bad).toBeNaN();
    }
  });
});

describe('itemFromExercise', () => {
  it('pre-fills a rep range for a rep exercise and none for a timed one', () => {
    expect(itemFromExercise(squat, 'a')).toMatchObject({
      sets: '3',
      repsMin: '8',
      repsMax: '12',
      rest: '90',
    });
    expect(itemFromExercise(plank, 'b')).toMatchObject({
      sets: '3',
      repsMin: '',
      repsMax: '',
      duration: '',
    });
  });
});

describe('validateDraft', () => {
  it('accepts a named draft with defaults', () => {
    expect(validateDraft({ ...emptyDraft(), name: 'Legs', items: [item()] })).toEqual({});
  });

  it('needs a name', () => {
    expect(validateDraft(emptyDraft())).toHaveProperty('name');
    expect(validateDraft({ ...emptyDraft(), name: 'x'.repeat(121) })).toHaveProperty('name');
  });

  it('reports each bad number under the item key', () => {
    const errors = validateDraft({
      ...emptyDraft(),
      name: 'Legs',
      items: [item({ sets: '0', rest: '4000' }), item({ key: 'k2', repsMin: '12', repsMax: '8' })],
    });
    expect(Object.keys(errors).sort()).toEqual(['k1.rest', 'k1.sets', 'k2.repsMin']);
  });

  it('lets targets stay empty', () => {
    const blank = item({ sets: '', repsMin: '', repsMax: '', rest: '' });
    expect(validateDraft({ ...emptyDraft(), name: 'Legs', items: [blank] })).toEqual({});
  });
});

describe('toRoutineInput', () => {
  it('sends numbers, nulls for empty fields and only the fields of the tracking mode', () => {
    const input = toRoutineInput({
      name: '  Legs ',
      weekday: 3,
      note: 'heavy',
      items: [
        item({ sets: '4', repsMin: '6', repsMax: '10', rest: '120', duration: '30' }),
        { ...itemFromExercise(plank, 'k2'), sets: '', duration: '60', repsMin: '5', rest: '' },
      ],
    });
    expect(input).toEqual({
      name: 'Legs',
      weekday: 3,
      note: 'heavy',
      exercises: [
        {
          exercise_id: 'Barbell_Squat',
          target_sets: 4,
          target_reps_min: 6,
          target_reps_max: 10,
          target_duration_seconds: null,
          rest_seconds: 120,
          note: '',
        },
        {
          exercise_id: 'Plank',
          target_sets: null,
          target_reps_min: null,
          target_reps_max: null,
          target_duration_seconds: 60,
          rest_seconds: null,
          note: '',
        },
      ],
    });
  });
});

describe('draftFromRoutine', () => {
  it('round-trips a stored routine', () => {
    const routine: Routine = {
      id: '22222222-2222-4222-8222-222222222222',
      name: 'Pull',
      weekday: 1,
      position: 1,
      note: '',
      exercises: [
        {
          id: 'e1',
          exercise_id: 'Barbell_Squat',
          position: 1,
          target_sets: 4,
          target_reps_min: 6,
          target_reps_max: 10,
          target_duration_seconds: null,
          rest_seconds: 120,
          note: 'slow',
          exercise_name: 'Barbell Squat',
          tracking_mode: 'weight_reps',
          images: ['Barbell_Squat/0.jpg'],
          primary_muscles: ['quadriceps'],
        },
      ],
    };
    const draft = draftFromRoutine(routine);
    expect(draft.items[0]).toMatchObject({
      key: 'e1',
      sets: '4',
      repsMin: '6',
      repsMax: '10',
      duration: '',
    });
    expect(toRoutineInput(draft).exercises[0]).toMatchObject({
      exercise_id: 'Barbell_Squat',
      target_sets: 4,
      target_reps_min: 6,
      target_reps_max: 10,
      rest_seconds: 120,
      note: 'slow',
    });
  });
});

describe('moveItem', () => {
  it('moves within bounds and ignores a move past either end', () => {
    expect(moveItem(['a', 'b', 'c'], 0, 1)).toEqual(['b', 'a', 'c']);
    expect(moveItem(['a', 'b', 'c'], 2, -1)).toEqual(['a', 'c', 'b']);
    expect(moveItem(['a', 'b', 'c'], 0, -1)).toEqual(['a', 'b', 'c']);
    expect(moveItem(['a', 'b', 'c'], 2, 1)).toEqual(['a', 'b', 'c']);
  });

  it('does not change its input', () => {
    const items = ['a', 'b'];
    moveItem(items, 0, 1);
    expect(items).toEqual(['a', 'b']);
  });
});

describe('formatSeconds and targetSummary', () => {
  it('formats rest', () => {
    expect(formatSeconds(45)).toBe('45 s');
    expect(formatSeconds(120)).toBe('2 min');
    expect(formatSeconds(90)).toBe('1 min 30 s');
  });

  const base = {
    target_sets: 4,
    target_reps_min: 6,
    target_reps_max: 10,
    target_duration_seconds: null,
    rest_seconds: 120,
  };

  it('describes sets, the rep range and rest', () => {
    expect(targetSummary(base)).toBe('4 × 6–10, rest 2 min');
    expect(targetSummary({ ...base, target_reps_max: 6 })).toBe('4 × 6, rest 2 min');
    expect(targetSummary({ ...base, target_reps_max: null })).toBe('4 × 6+, rest 2 min');
  });

  it('describes a timed exercise and missing targets', () => {
    const timed = {
      ...base,
      target_reps_min: null,
      target_reps_max: null,
      target_duration_seconds: 60,
    };
    expect(targetSummary(timed)).toBe('4 × 1 min, rest 2 min');
    expect(targetSummary({ ...timed, target_duration_seconds: null })).toBe('4 sets, rest 2 min');
    expect(targetSummary({ ...timed, target_sets: null, target_duration_seconds: null })).toBe(
      'rest 2 min',
    );
  });
});

describe('linesOf', () => {
  it('keeps non-empty trimmed lines', () => {
    expect(linesOf(' one \n\n two\n')).toEqual(['one', 'two']);
    expect(linesOf('')).toEqual([]);
  });
});
