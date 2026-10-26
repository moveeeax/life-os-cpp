import { describe, expect, it } from 'vitest';

import type { NowWeather, Today } from '@/lib/now/types';

import { greeting, hourOf, plural } from './greet';

const base: Today = {
  tasks: { late_count: 0, first_late: null, late: [], today: [], inbox_count: 0 },
  goals: [],
  focus: null,
  errors: [],
};
const health = (sleep_minutes: number | null): Today['health'] => ({
  steps: null,
  active_kcal: null,
  resting_bpm: null,
  sleep: null,
  sleep_minutes,
  weight_kg: null,
  weight_change_7d: null,
});
const weather = (code: number): NowWeather => ({
  place: 'Almaty',
  temperature: 8,
  min: null,
  max: null,
  code,
  words: '',
  is_day: true,
  time: '2026-10-26T09:00',
});
const late = (n: number): Today => ({
  ...base,
  tasks: { late_count: n, first_late: null, late: [], today: [], inbox_count: 0 },
});

describe('plural', () => {
  it('picks the Russian form for the number', () => {
    const f = (n: number) => plural(n, 'задача', 'задачи', 'задач');
    expect([1, 2, 5, 11, 12, 21, 22, 25, 111].map(f)).toEqual([
      'задача',
      'задачи',
      'задач',
      'задач',
      'задач',
      'задача',
      'задачи',
      'задач',
      'задач',
    ]);
  });
});

describe('greeting', () => {
  it('says the recorded hello of the hour with nothing from the day', () => {
    expect(greeting(base, undefined, 15, false)).toEqual({
      text: 'Привет! Как идут дела?',
      recorded: 'day',
    });
    expect(hourOf(2)).toBe('night');
    expect(hourOf(23)).toBe('night');
  });

  it('names late tasks first, without the count while numbers are hidden', () => {
    expect(greeting(late(1), weather(61), 9, false).text).toBe(
      'Доброе утро! Одна задача просрочена, глянешь?',
    );
    expect(greeting(late(3), undefined, 15, false).text).toBe(
      'Привет! 3 задачи просрочены, глянешь?',
    );
    expect(greeting(late(5), undefined, 19, false).text).toBe(
      'Добрый вечер! 5 задач просрочено, глянешь?',
    );
    expect(greeting(late(3), undefined, 15, true).text).toBe(
      'Привет! Есть просроченные задачи, глянешь?',
    );
  });

  it('warns of rain or snow', () => {
    expect(greeting(base, weather(63), 15, false).text).toBe(
      'Привет! Сегодня дождь, зонт не забудь.',
    );
    expect(greeting(base, weather(73), 15, false).text).toBe(
      'Привет! Сегодня снег, оденься потеплее.',
    );
    expect(greeting(base, weather(3), 15, false).recorded).toBe('day');
  });

  it('speaks of the night in the morning only, and not while numbers are hidden', () => {
    const t = { ...base, health: health(412) };
    expect(greeting(t, undefined, 8, false).text).toBe(
      'Доброе утро! 6 часов 52 минуты сна. Береги себя сегодня.',
    );
    expect(greeting({ ...base, health: health(480) }, undefined, 8, false).text).toBe(
      'Доброе утро! 8 часов сна. Бодрячком?',
    );
    expect(greeting(t, undefined, 15, false).recorded).toBe('day');
    expect(greeting(t, undefined, 8, true).recorded).toBe('morning');
  });
});
