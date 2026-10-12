import { describe, expect, it } from 'vitest';

import { cacheKey, localDate, readCache, writeCache, type KeyValueStore } from './cache';

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
