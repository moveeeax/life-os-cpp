import { describe, expect, it } from 'vitest';

import {
  dayCount,
  formatValue,
  goalLine,
  habitBody,
  habitForm,
  habitProblem,
  lastDays,
  longDay,
  mondayOf,
  scheduleWords,
  splitNumbers,
  subline,
  canGoBack,
  oldestTickDay,
  amountProblem,
  hasTarget,
  moodBody,
  moodForm,
  progressLine,
  quickAmounts,
  targetLine,
  toggleTag,
  weekCells,
  weekDates,
  weekLine,
  type HabitDays,
  type SourceInfo,
} from './index';

const habit = (over: Partial<HabitDays>): HabitDays => ({
  id: 'h1',
  name: 'Meditation',
  schedule: 'daily',
  per_week: null,
  goal: null,
  unit: '',
  source: 'manual',
  direction: 'at_least',
  target: null,
  position: 1,
  archived_at: null,
  created_at: '2026-10-01T00:00:00Z',
  days: [],
  streak: 0,
  best: 0,
  week_done: null,
  no_data: false,
  ...over,
});

const day = (date: string, done: boolean, manual = done, value: number | null = null) => ({
  date,
  done,
  manual,
  value,
});

describe('the week', () => {
  // 2026-10-14 is a Wednesday; 2026-01-01 a Thursday.
  it('runs Monday to Sunday', () => {
    expect(mondayOf('2026-10-14')).toBe('2026-10-12');
    expect(mondayOf('2026-10-12')).toBe('2026-10-12');
    expect(mondayOf('2026-10-18')).toBe('2026-10-12');
    expect(mondayOf('2026-01-01')).toBe('2025-12-29');
    expect(weekDates('2025-12-29')).toEqual([
      '2025-12-29',
      '2025-12-30',
      '2025-12-31',
      '2026-01-01',
      '2026-01-02',
      '2026-01-03',
      '2026-01-04',
    ]);
    expect(weekLine('2026-10-12')).toBe('Oct 12 – Oct 18');
    expect(longDay('2026-10-14')).toBe('Wednesday, Oct 14');
  });

  it('draws a cell per day: by hand, from data, open, after today', () => {
    const h = habit({
      days: [
        day('2026-10-12', true, true),
        day('2026-10-13', true, false, 9312),
        day('2026-10-14', false, false, 4000),
      ],
    });
    expect(weekCells(h, '2026-10-12', '2026-10-14')).toEqual([
      'manual',
      'auto',
      'open',
      'future',
      'future',
      'future',
      'future',
    ]);
    // A past week the answer does not carry is open, not missing.
    expect(weekCells(habit({}), '2026-10-05', '2026-10-14')).toEqual(Array(7).fill('open'));
  });
});

describe('the words of a habit', () => {
  it('says how often', () => {
    expect(scheduleWords({ schedule: 'daily', per_week: null })).toBe('every day');
    expect(scheduleWords({ schedule: 'weekly', per_week: 1 })).toBe('once a week');
    expect(scheduleWords({ schedule: 'weekly', per_week: 2 })).toBe('twice a week');
    expect(scheduleWords({ schedule: 'weekly', per_week: 3 })).toBe('3 times a week');
  });

  it('says where the ticks come from', () => {
    expect(subline({ schedule: 'daily', per_week: null, source: 'manual' })).toBe('every day');
    expect(subline({ schedule: 'daily', per_week: null, source: 'fitness:steps' })).toBe(
      'from Mi · every day',
    );
    expect(subline({ schedule: 'weekly', per_week: 4, source: 'workout:sessions' })).toBe(
      'from Training · 4 times a week',
    );
    expect(
      subline({ schedule: 'daily', per_week: null, source: 'fitness:sleep', no_data: true }),
    ).toBe('no data from Mi · every day');
    expect(subline({ schedule: 'daily', per_week: null, source: 'fitness:weight' })).toBe(
      'from Mi · every day',
    );
    expect(subline({ schedule: 'daily', per_week: null, source: 'food:kcal' })).toBe(
      'from Food · every day',
    );
    expect(subline({ schedule: 'daily', per_week: null, source: 'money:expense:KZT' })).toBe(
      'from Money · every day',
    );
  });

  it('keeps the digits of a line apart for Hide numbers', () => {
    expect(splitNumbers('from Training · 4 times a week')).toEqual([
      { text: 'from Training · ', num: false },
      { text: '4', num: true },
      { text: ' times a week', num: false },
    ]);
    expect(splitNumbers('every day')).toEqual([{ text: 'every day', num: false }]);
  });

  it('writes a value in its own terms', () => {
    // Sleep comes in hours: 7.7 h = 462 min.
    expect(formatValue({ source: 'fitness:sleep', unit: '' }, 7.7)).toBe('7 h 42 m');
    expect(formatValue({ source: 'fitness:sleep', unit: '' }, 0.75)).toBe('45 m');
    expect(formatValue({ source: 'fitness:steps', unit: '' }, 9312)).toBe('9,312');
    expect(formatValue({ source: 'fitness:active_kcal', unit: '' }, 512.4)).toBe('512 kcal');
    expect(formatValue({ source: 'workout:sessions', unit: '' }, 1)).toBe('1 session');
    expect(formatValue({ source: 'workout:sessions', unit: '' }, 2)).toBe('2 sessions');
    expect(formatValue({ source: 'manual', unit: 'min' }, 20)).toBe('20 min');
    expect(formatValue({ source: 'fitness:weight', unit: '' }, 71.84)).toBe('71.8 kg');
    expect(formatValue({ source: 'fitness:weight', unit: '' }, 72)).toBe('72 kg');
    expect(goalLine({ source: 'fitness:steps', goal: 8000, unit: '' })).toBe('8,000 steps');
    expect(goalLine({ source: 'fitness:sleep', goal: 7.5, unit: '' })).toBe('7.5 h');
    expect(goalLine({ source: 'workout:sessions', goal: null, unit: '' })).toBeNull();
    expect(goalLine({ source: 'fitness:weight', goal: 80, unit: '' })).toBe('80 kg');
    expect(goalLine({ source: 'workout:sessions', goal: 2, unit: '' })).toBe('2 sessions');
    expect(goalLine({ source: 'food:kcal', goal: 2200, unit: 'kcal' })).toBe('2,200 kcal');
    expect(formatValue({ source: 'food:kcal', unit: 'kcal' }, 2100)).toBe('2,100 kcal');
  });
});

describe('the day', () => {
  // Meditation done, Reading open; Swimming 2 of 2 this week (met), Workout 1 of 3.
  it('counts as the Now plate does', () => {
    const list = [
      habit({ id: 'a', days: [day('2026-10-14', true)] }),
      habit({ id: 'b', name: 'Reading', days: [day('2026-10-14', false)] }),
      habit({ id: 'c', schedule: 'weekly', per_week: 2, week_done: 2, days: [] }),
      habit({ id: 'd', schedule: 'weekly', per_week: 3, week_done: 1, days: [] }),
    ];
    expect(dayCount(list, '2026-10-14')).toEqual({ done: 2, total: 4, left: 1 });
  });

  it('gives the last 30 days oldest first', () => {
    const strip = lastDays(
      habit({ days: [day('2026-10-14', true), day('2026-09-15', true)] }),
      '2026-10-14',
    );
    expect(strip).toHaveLength(30);
    expect(strip[0]).toBe(true);
    expect(strip[29]).toBe(true);
    expect(strip.filter(Boolean)).toHaveLength(2);
  });
});

describe('the habit form', () => {
  it('sends a weekly habit with its count and unit', () => {
    expect(
      habitBody({
        name: ' Swimming ',
        schedule: 'weekly',
        per_week: '3',
        goal: '45',
        unit: 'min',
        source: 'manual',
        target: '',
        direction: 'at_least',
      }),
    ).toEqual({
      name: 'Swimming',
      schedule: 'weekly',
      per_week: 3,
      goal: 45,
      unit: 'min',
      source: 'manual',
      target: null,
      direction: 'at_least',
    });
  });

  const kcal: SourceInfo = { unit: 'kcal', presence: false };
  const weight: SourceInfo = { unit: 'kg', presence: true };

  it('sends the catalog id, its unit and the direction of a sourced habit', () => {
    const f = {
      ...habitForm(),
      name: 'Calories',
      per_week: '4',
      goal: '2200',
      source: 'food:kcal',
    };
    expect(habitBody({ ...f, direction: 'at_most' }, kcal)).toEqual({
      name: 'Calories',
      schedule: 'daily',
      per_week: null,
      goal: 2200,
      unit: 'kcal',
      source: 'food:kcal',
      target: null,
      direction: 'at_most',
    });
    // A habit by hand always compares at least.
    expect(habitBody({ ...f, source: 'manual', direction: 'at_most' }).direction).toBe('at_least');
  });

  it('sends no goal for a presence source left empty, and a goal when one is given', () => {
    const f = { ...habitForm(), name: 'Weigh-in', source: 'fitness:weight' };
    expect(habitBody(f, weight).goal).toBeNull();
    expect(habitProblem(f, weight)).toBeNull();
    const capped = { ...f, goal: '80', direction: 'at_most' as const };
    expect(habitBody(capped, weight)).toMatchObject({ goal: 80, unit: 'kg', direction: 'at_most' });
  });

  it('asks for a goal for a sum or an average', () => {
    expect(habitProblem({ ...habitForm(), name: 'Steps', source: 'fitness:steps' }, kcal)).toBe(
      'This source needs a goal to compare the day with.',
    );
    // Without the catalog the API is left to say it.
    expect(habitProblem({ ...habitForm(), name: 'Steps', source: 'fitness:steps' })).toBeNull();
    expect(
      habitProblem({ ...habitForm(), name: 'Sleep', source: 'fitness:sleep', goal: '25' }),
    ).toBe('A goal in hours is at most 24.');
    expect(habitProblem({ ...habitForm(), name: '' })).toBe('Give the habit a name.');
    expect(
      habitProblem({ ...habitForm(), name: 'Sleep', source: 'fitness:sleep', goal: '8' }),
    ).toBeNull();
  });
});

describe('how far back a tick reaches', () => {
  it('stops at today minus 366 days', () => {
    expect(oldestTickDay('2026-10-18')).toBe('2025-10-17');
    // The week of 2025-10-20 has the week of 2025-10-13 before it, whose Sunday is reachable.
    expect(canGoBack('2025-10-20', '2026-10-18')).toBe(true);
    // The week before 2025-10-13 ends on 2025-10-12, out of reach.
    expect(canGoBack('2025-10-13', '2026-10-18')).toBe(false);
  });
});

describe('a daily target', () => {
  const water = habit({ name: 'Water', unit: 'ml', target: 2000 });

  it('is sent for a habit by hand only', () => {
    const f = { ...habitForm(), name: 'Water', unit: 'ml', target: '2000' };
    expect(habitBody(f).target).toBe(2000);
    expect(habitBody({ ...f, target: ' ' }).target).toBeNull();
    expect(habitBody({ ...f, source: 'fitness:steps', goal: '8000' }).target).toBeNull();
    expect(habitForm(water).target).toBe('2000');
    expect(habitProblem({ ...f, target: '0' })).toBe(
      'The daily target is a number above zero, at most 1,000,000.',
    );
    expect(habitProblem({ ...f, target: 'lots' })).not.toBeNull();
    expect(habitProblem(f)).toBeNull();
  });

  it('reads as the sum of the day against the target', () => {
    expect(hasTarget(water)).toBe(true);
    expect(hasTarget(habit({}))).toBe(false);
    expect(hasTarget(habit({ source: 'fitness:steps', target: 10 }))).toBe(false);
    expect(progressLine(water, 750)).toBe('750 of 2,000 ml');
    expect(progressLine(water, null)).toBe('0 of 2,000 ml');
    expect(targetLine(water)).toBe('2,000 ml');
    expect(targetLine(habit({}))).toBeNull();
    expect(quickAmounts(water)).toEqual([250, 500]);
    expect(quickAmounts(habit({ unit: 'pages' }))).toEqual([]);
  });

  it('checks an amount to add', () => {
    expect(amountProblem('250')).toBeNull();
    expect(amountProblem('0,5')).toBeNull();
    expect(amountProblem('')).toBe('An amount above zero.');
    expect(amountProblem('-1')).toBe('An amount above zero.');
    expect(amountProblem('2000000')).toBe('At most 1,000,000 at a time.');
  });

  it('shows a day short of the target as half a cell', () => {
    const h = habit({
      target: 2000,
      days: [day('2026-10-12', false, true, 500), day('2026-10-13', true, true, 2000)],
    });
    expect(weekCells(h, '2026-10-12', '2026-10-14').slice(0, 3)).toEqual([
      'partial',
      'manual',
      'open',
    ]);
  });
});

describe('the mood form', () => {
  it('waits for a score and keeps the tags in the set order', () => {
    expect(moodBody(moodForm())).toBeNull();
    const tags = toggleTag(toggleTag(['tired'], 'calm'), 'focused');
    expect(tags).toEqual(['calm', 'focused', 'tired']);
    expect(toggleTag(tags, 'focused')).toEqual(['calm', 'tired']);
    expect(moodBody({ score: 4, tags, note: ' walk ' })).toEqual({
      score: 4,
      tags: ['calm', 'focused', 'tired'],
      note: 'walk',
    });
    const m = {
      date: '2026-10-18',
      score: 2,
      tags: ['sick' as const],
      note: '',
      created_at: '2026-10-18T09:00:00+00:00',
      updated_at: '2026-10-18T09:00:00+00:00',
    };
    expect(moodForm(m)).toEqual({ score: 2, tags: ['sick'], note: '' });
  });
});
