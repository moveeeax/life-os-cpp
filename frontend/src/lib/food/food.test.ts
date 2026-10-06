import { describe, expect, it } from 'vitest';

import {
  draftFromJob,
  formatGrams,
  formatKcal,
  linesToEntries,
  mondayOf,
  parseDateParam,
  parseErrorText,
  percent,
  scaled,
  shiftDate,
  todayLocal,
  weekSeries,
  type ParseDraftLine,
} from './index';
import type { FoodParseJob, FoodParseLine, FoodWeek } from './types';

describe('todayLocal', () => {
  it('formats the local calendar date, not the UTC one', () => {
    // 23:30 local on the 5th; toISOString would be the 6th in a zone behind UTC.
    const now = new Date(2026, 9, 5, 23, 30);
    expect(todayLocal(now)).toBe('2026-10-05');
  });
});

describe('parseDateParam', () => {
  it('keeps a valid date and falls back otherwise', () => {
    expect(parseDateParam('2026-02-28', 'x')).toBe('2026-02-28');
    expect(parseDateParam(null, '2026-10-05')).toBe('2026-10-05');
    expect(parseDateParam('2026-13-01', '2026-10-05')).toBe('2026-10-05');
    expect(parseDateParam('2026-02-30', '2026-10-05')).toBe('2026-10-05');
    expect(parseDateParam('yesterday', '2026-10-05')).toBe('2026-10-05');
  });
});

describe('shiftDate', () => {
  it('crosses month and year boundaries', () => {
    expect(shiftDate('2026-10-31', 1)).toBe('2026-11-01');
    expect(shiftDate('2026-01-01', -1)).toBe('2025-12-31');
    expect(shiftDate('2026-10-05', 7)).toBe('2026-10-12');
  });
});

describe('mondayOf', () => {
  it('returns the Monday of the week the date is in', () => {
    expect(mondayOf('2026-10-05')).toBe('2026-10-05'); // a Monday
    expect(mondayOf('2026-10-11')).toBe('2026-10-05'); // the Sunday after it
    expect(mondayOf('2026-10-08')).toBe('2026-10-05');
  });
});

describe('scaled', () => {
  it('scales per-100 numbers to the grams with one decimal', () => {
    const r = scaled({ kcal: 155, protein_g: 13, fat_g: 11, carbs_g: 1.1 }, 60);
    expect(r).toEqual({ kcal: 93, protein_g: 7.8, fat_g: 6.6, carbs_g: 0.7 });
  });
  it('is zero for zero grams', () => {
    expect(scaled({ kcal: 155, protein_g: 13, fat_g: 11, carbs_g: 1.1 }, 0).kcal).toBe(0);
  });
});

describe('percent', () => {
  it('is null without a target and may exceed 100', () => {
    expect(percent(500, null)).toBeNull();
    expect(percent(500, undefined)).toBeNull();
    expect(percent(500, 0)).toBeNull();
    expect(percent(500, 1000)).toBe(50);
    expect(percent(1500, 1000)).toBe(150);
  });
});

describe('format', () => {
  it('rounds kcal and keeps one decimal for grams', () => {
    expect(formatKcal(1667.4)).toBe('1,667');
    expect(formatGrams(7.84)).toBe('7.8');
    expect(formatGrams(150)).toBe('150');
  });
});

const line = (over: Partial<FoodParseLine> = {}): FoodParseLine => ({
  name: 'Rice',
  grams: 150,
  kcal: 195,
  protein_g: 4,
  fat_g: 0.5,
  carbs_g: 42,
  item_id: null,
  estimated: true,
  note: '',
  ...over,
});

const job = (result: FoodParseLine[] | null): FoodParseJob => ({
  id: 'j1',
  status: 'done',
  text: 'rice and eggs',
  meal: 'lunch',
  date: '2026-10-05',
  result,
  error: null,
  model: 'm',
  prompt_tokens: 1,
  completion_tokens: 1,
  created_at: '2026-10-05T10:00:00Z',
  finished_at: '2026-10-05T10:00:05Z',
});

describe('draftFromJob', () => {
  it('selects every line and gives each a key', () => {
    const draft = draftFromJob(job([line(), line({ name: 'Eggs' })]));
    expect(draft).toHaveLength(2);
    expect(draft.every((l) => l.selected)).toBe(true);
    expect(new Set(draft.map((l) => l.key)).size).toBe(2);
  });
  it('is empty without a result', () => {
    expect(draftFromJob(job(null))).toEqual([]);
  });
});

describe('linesToEntries', () => {
  const draft: ParseDraftLine[] = [
    { ...line({ item_id: '11111111-1111-4111-8111-111111111111' }), key: 'a', selected: true },
    { ...line({ name: 'Eggs', kcal: 140 }), key: 'b', selected: true },
    { ...line({ name: 'Skipped' }), key: 'c', selected: false },
  ];
  it('sends item lines by id and grams, estimated lines by numbers', () => {
    const entries = linesToEntries(draft, '2026-10-05', 'lunch');
    expect(entries).toEqual([
      {
        date: '2026-10-05',
        meal: 'lunch',
        item_id: '11111111-1111-4111-8111-111111111111',
        grams: 150,
        note: '',
      },
      {
        date: '2026-10-05',
        meal: 'lunch',
        name: 'Eggs',
        grams: 150,
        kcal: 140,
        protein_g: 4,
        fat_g: 0.5,
        carbs_g: 42,
        note: '',
      },
    ]);
  });
});

describe('parseErrorText', () => {
  it('maps known codes and strips the code of others', () => {
    expect(parseErrorText('invalid_answer: lines must not be empty')).toBe(
      'The model did not answer with a usable list: lines must not be empty',
    );
    expect(parseErrorText('not_configured')).toBe('Text parsing is not configured on the server.');
    expect(parseErrorText('provider_error_502: upstream')).toBe(
      'The language model provider answered with an error (502).',
    );
    expect(parseErrorText('something_else: detail')).toBe('detail');
    expect(parseErrorText(null)).toBe('The parse failed.');
  });
});

describe('weekSeries', () => {
  const week: FoodWeek = {
    from: '2026-10-05',
    days: [
      { date: '2026-10-05', kcal: 1500, protein_g: 100, fat_g: 50, carbs_g: 120, entries: 3 },
      { date: '2026-10-06', kcal: 0, protein_g: 0, fat_g: 0, carbs_g: 0, entries: 0 },
    ],
    targets: null,
  };
  it('lists the days and a null target without goals', () => {
    expect(weekSeries(week)).toEqual({
      days: ['2026-10-05', '2026-10-06'],
      kcal: [1500, 0],
      target: null,
    });
  });
  it('carries the kcal target', () => {
    const w = { ...week, targets: { kcal: 1667, protein_g: 150, fat_g: 60, carbs_g: 130 } };
    expect(weekSeries(w).target).toBe(1667);
  });
});
