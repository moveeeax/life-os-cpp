// @vitest-environment jsdom
import { beforeEach, describe, expect, it } from 'vitest';

import { readJson, writeJson } from '@/lib/browserStore';

import { markOn, readMarks, seenOn } from './once';

beforeEach(() => localStorage.clear());

describe('readJson / writeJson', () => {
  it('round-trips a value and falls back when absent or unreadable', () => {
    expect(readJson('k', [1])).toEqual([1]);
    writeJson('k', { a: 2 });
    expect(localStorage.getItem('k')).toBe('{"a":2}');
    expect(readJson('k', null)).toEqual({ a: 2 });
    localStorage.setItem('k', '{not json');
    expect(readJson('k', 'x')).toBe('x');
  });
});

describe('marks', () => {
  it('keeps one JSON map per store, in the format the lines wrote before', () => {
    markOn('now:ani:life', 'energy', '2026-11-15');
    markOn('now:ani:life', 'quest', '2026-11-15');
    expect(JSON.parse(localStorage.getItem('now:ani:life')!)).toEqual({
      energy: '2026-11-15',
      quest: '2026-11-15',
    });
    expect(seenOn('now:ani:life', 'energy')).toBe('2026-11-15');
    expect(seenOn('now:ani:life2', 'energy')).toBeUndefined();
  });

  it('reads a broken map as empty', () => {
    localStorage.setItem('now:ani:life', 'null');
    expect(readMarks('now:ani:life')).toEqual({});
  });
});
