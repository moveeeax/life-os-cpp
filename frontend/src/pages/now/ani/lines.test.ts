// @vitest-environment jsdom
import { afterEach, describe, expect, it } from 'vitest';

import { eventOfLine, inboxKind, quietKind, snooze } from './lines';

afterEach(() => localStorage.clear());

const day = {
  date: '2026-10-29',
  calendar: 'ok',
  events: [
    {
      id: 'e1',
      title: 'Call with Anna',
      start: '2026-10-29T06:00:00Z',
      end: '2026-10-29T07:00:00Z',
      all_day: false,
      location: '',
    },
    {
      id: 'e2',
      title: 'Стендап',
      start: '2026-10-29T04:00:00Z',
      end: '2026-10-29T04:30:00Z',
      all_day: false,
      location: '',
    },
  ],
  free: [],
  focus: [],
  workouts: [],
  tasks: [],
} as never;

describe('lines', () => {
  it('MutedKindSilent: muted on the server or put off for an hour', () => {
    expect(quietKind('stretch', ['stretch'])).toBe(true);
    expect(quietKind('stretch', [])).toBe(false);
    snooze('brief', 1000);
    expect(quietKind('brief', [], 2000)).toBe(true);
    expect(quietKind('brief', [], 1000 + 61 * 60_000)).toBe(false);
    expect(quietKind(undefined, ['stretch'])).toBe(false);
    expect(inboxKind('event', 'telegram')).toBe('telegram');
    expect(inboxKind('remind', undefined)).toBe('reminder');
  });

  it('PointsAtEvent: by its title or its start time', () => {
    expect(eventOfLine('В 11 звонок с Анной', day, 'Asia/Tashkent')).toBe('e1');
    expect(eventOfLine('Через час call with anna', day, 'Asia/Tashkent')).toBe('e1');
    expect(eventOfLine('Стендап уже скоро', day, 'Asia/Tashkent')).toBe('e2');
    expect(eventOfLine('Ты спал 7 часов', day, 'Asia/Tashkent')).toBeNull();
  });
});
