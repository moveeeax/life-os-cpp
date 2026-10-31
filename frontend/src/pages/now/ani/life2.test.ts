import { describe, expect, it } from 'vitest';

import {
  bedtimeLine,
  bedtimeOf,
  birthdayToday,
  brokenStreakOf,
  followUpOf,
  guessOptions,
  mealOptions,
  moodLow,
  nightOf,
  questionOfDay,
  QUESTIONS,
  sleepRecord,
  stepsRecord,
  tabTip,
  tripHello,
  unusualSpend,
  windDownNow,
  wishDue,
} from './life2';

const night = (date: string, start: string, minutes: number, nap = false) => ({
  start_at: `${date}T${start}:00Z`,
  end_at: `${date}T23:59:00Z`,
  duration_minutes: minutes,
  is_nap: nap,
});

describe("Ani's life, part two", () => {
  it('QuestionOfTheDay: a new one each day', () => {
    expect(QUESTIONS).toContain(questionOfDay('2026-10-31'));
    expect(questionOfDay('2026-10-31')).not.toBe(questionOfDay('2026-11-01'));
  });

  it('FollowUpAndWishes: from her dated facts', () => {
    const facts = [
      { id: 'a', text: '2026-10-30: врач' },
      { id: 'b', text: 'Хочу: наушники за 200 долларов (2026-09-20)' },
      { id: 'c', text: 'Хочу: кофемашину (2026-10-25)' },
    ];
    expect(followUpOf(facts, '2026-10-31')).toEqual({ id: 'a', what: 'врач' });
    expect(followUpOf(facts, '2026-11-02')).toBeNull();
    expect(wishDue(facts, '2026-10-31')?.what).toBe('наушники за 200 долларов');
  });

  it('MoodMirror: a low check-in today', () => {
    expect(moodLow([{ date: '2026-10-31', score: 2 }], '2026-10-31')).toBe(true);
    expect(moodLow([{ date: '2026-10-30', score: 1 }], '2026-10-31')).toBe(false);
  });

  it('Bedtime: the usual hour, the half hour before it, and the morning word', () => {
    const nights = ['23:10', '23:30', '23:40', '00:20', '23:20', '23:35'].map((t, i) =>
      night(`2026-10-${20 + i}`, t, 420),
    );
    const usual = bedtimeOf(nights, 'UTC')!;
    expect(usual).toBe(nightOf(23, 35));
    expect(windDownNow(nightOf(23, 10), usual)).toBe(true);
    expect(windDownNow(nightOf(22, 0), usual)).toBe(false);
    expect(bedtimeLine(night('2026-10-30', '22:30', 480), usual, 'UTC')).toMatch(/1 ч раньше/);
    expect(bedtimeLine(night('2026-10-30', '23:40', 480), usual, 'UTC')).toBeNull();
    expect(bedtimeOf(nights.slice(0, 3), 'UTC')).toBeNull();
  });

  it('LighterAfterABrokenStreak: three days done, yesterday missed', () => {
    const days = (pattern: boolean[]) =>
      pattern.map((done, i) => ({ date: `2026-10-${27 + i}`, done }));
    const h = {
      id: 'h',
      name: 'Медитация',
      schedule: 'daily',
      days: days([true, true, true, false]),
    };
    expect(brokenStreakOf([h], '2026-10-31')).toBe('Медитация');
    expect(
      brokenStreakOf([{ ...h, days: days([true, false, true, false]) }], '2026-10-31'),
    ).toBeNull();
  });

  it('PersonalRecords: the longest night and the most steps of the month', () => {
    const nights = Array.from({ length: 8 }, (_, i) => ({
      ...night(`2026-10-${10 + i}`, '23:00', 400),
      end_at: `2026-10-${11 + i}T07:00:00Z`,
    }));
    nights[7].duration_minutes = 560;
    expect(sleepRecord(nights)).toMatch(/9 ч 20 мин/);
    nights[2].duration_minutes = 600;
    expect(sleepRecord(nights)).toBeNull();
    const series = Array.from({ length: 10 }, (_, i) => ({
      date: `2026-10-${21 + i}`,
      value: 6000,
    }));
    series[9].value = 14000;
    expect(stepsRecord(series, '2026-10-31')).toMatch(/14\s000/);
  });

  it('WhatToEat: two protein-rich recent products that fit the kcal left', () => {
    const items = [
      { id: 'skyr', name: 'Skyr', kcal: 60, protein_g: 11, servings: [{ grams: 150 }] },
      { id: 'chicken', name: 'Chicken', kcal: 165, protein_g: 31, servings: [] },
      { id: 'bread', name: 'Bread', kcal: 250, protein_g: 8, servings: [] },
      { id: 'cake', name: 'Cake', kcal: 400, protein_g: 6, servings: [{ grams: 200 }] },
    ];
    expect(mealOptions({ kcal: 500, protein_g: 40 }, items).map((o) => o.item.id)).toEqual([
      'chicken',
      'skyr',
    ]);
    expect(mealOptions({ kcal: 100, protein_g: 40 }, items).map((o) => o.item.id)).toEqual([
      'skyr',
    ]);
  });

  it('UnusualSpend: three times the median of the category', () => {
    expect(unusualSpend(-90000, [-20000, -25000, -18000, -30000, -22000])).toEqual({
      median: 22000,
    });
    expect(unusualSpend(-40000, [-20000, -25000, -18000, -30000, -22000])).toBeNull();
    expect(unusualSpend(-90000, [-20000])).toBeNull();
  });

  it('TripHello: the local hello and a price in the home currency', () => {
    expect(tripHello('TH', 'THB', 'KZT', 14.2)).toMatch(
      /^Сават-ди! Для ориентира: 100 THB это примерно 1\s420 KZT\.$/,
    );
    expect(tripHello('XX', 'XXX', 'KZT', 1)).toBeNull();
  });

  it('GuessTheSteps: three options, one right', () => {
    const o = guessOptions(7400, '2026-10-31');
    expect(o).toHaveLength(3);
    expect(o.filter((x) => x.right).map((x) => x.label)).toEqual(['7 тыс']);
  });

  it('Birthday and the tip of the open tab', () => {
    expect(birthdayToday('1990-10-31', '2026-10-31')).toBe(true);
    expect(birthdayToday(null, '2026-10-31')).toBe(false);
    expect(tabTip('/tasks/inbox', { inbox: 3 })).toMatch(/входящих 3/);
    expect(tabTip('/money', { spent: { amount: 263275, currency: 'KZT' } })).toMatch(
      /263\s275 KZT/,
    );
    expect(tabTip('/goals', { stalled: 'Вес 75 кг' })).toMatch(/Вес 75 кг/);
    expect(tabTip('/notes', {})).toBeNull();
  });
});
