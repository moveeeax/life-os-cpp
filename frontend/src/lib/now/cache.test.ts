import { describe, expect, it } from 'vitest';

import {
  cacheKey,
  localDate,
  pruneDays,
  readCache,
  writeCache,
  type KeyValueStore,
  type ListableStore,
} from './cache';

function memory(): KeyValueStore {
  const m = new Map<string, string>();
  return {
    getItem: (k) => m.get(k) ?? null,
    setItem: (k, v) => void m.set(k, v),
  };
}

const blocked: KeyValueStore = {
  getItem: () => {
    throw new Error('blocked');
  },
  setItem: () => {
    throw new Error('blocked');
  },
};

describe('cacheKey', () => {
  it('differs per user and per date', () => {
    expect(cacheKey('u1', '2026-10-12')).toBe('now:u1:2026-10-12');
    expect(cacheKey('u1', '2026-10-13')).not.toBe(cacheKey('u1', '2026-10-12'));
  });
});

function listable(entries: Record<string, string>): ListableStore & { keys: () => string[] } {
  const m = new Map(Object.entries(entries));
  return {
    getItem: (k) => m.get(k) ?? null,
    setItem: (k, v) => void m.set(k, v),
    removeItem: (k) => void m.delete(k),
    key: (i) => [...m.keys()][i] ?? null,
    get length() {
      return m.size;
    },
    keys: () => [...m.keys()].sort(),
  };
}

describe('pruneDays', () => {
  it("keeps only the given day's digest of that user", () => {
    const s = listable({
      'now:u1:2026-10-10': '1',
      'now:u1:2026-10-11': '1',
      'now:u1:2026-10-12': '1',
      'now:u2:2026-10-10': '1',
      'now:settings:u1': '1',
      'now:focus:u1': '1',
      'now:wallpapers:desktop': '1',
      other: '1',
    });
    pruneDays('u1', '2026-10-12', s);
    expect(s.keys()).toEqual([
      'now:focus:u1',
      'now:settings:u1',
      'now:u1:2026-10-12',
      'now:u2:2026-10-10',
      'now:wallpapers:desktop',
      'other',
    ]);
  });

  it('survives a storage that throws or is absent', () => {
    expect(() => pruneDays('u1', '2026-10-12', undefined)).not.toThrow();
    const broken = {
      ...listable({}),
      key: () => {
        throw new Error('blocked');
      },
      length: 1,
    };
    expect(() => pruneDays('u1', '2026-10-12', broken)).not.toThrow();
  });
});

describe('localDate', () => {
  it('turns an instant into the day of a zone', () => {
    expect(localDate(new Date('2026-10-12T19:30:00Z'), 'Asia/Kolkata')).toBe('2026-10-13');
    expect(localDate(new Date('2026-10-12T19:30:00Z'), 'UTC')).toBe('2026-10-12');
  });
});

describe('readCache and writeCache', () => {
  it('round-trips a value', () => {
    const s = memory();
    writeCache('k', { a: 1 }, s);
    expect(readCache<{ a: number }>('k', s)).toEqual({ a: 1 });
  });

  it('gives null for a missing or broken value', () => {
    const s = memory();
    expect(readCache('missing', s)).toBeNull();
    s.setItem('broken', '{');
    expect(readCache('broken', s)).toBeNull();
  });

  it('survives a storage that throws or is absent', () => {
    expect(readCache('k', blocked)).toBeNull();
    expect(() => writeCache('k', 1, blocked)).not.toThrow();
    expect(readCache('k', undefined)).toBeNull();
    expect(() => writeCache('k', 1, undefined)).not.toThrow();
  });
});
