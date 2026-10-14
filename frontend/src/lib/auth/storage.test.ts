import { describe, expect, it } from 'vitest';

import { clearSignedInStorage } from './storage';

function store(entries: Record<string, string>) {
  const m = new Map(Object.entries(entries));
  return {
    getItem: (k: string) => m.get(k) ?? null,
    setItem: (k: string, v: string) => void m.set(k, v),
    removeItem: (k: string) => void m.delete(k),
    key: (i: number) => [...m.keys()][i] ?? null,
    get length() {
      return m.size;
    },
    keys: () => [...m.keys()].sort(),
  };
}

describe('clearSignedInStorage', () => {
  it("drops the signed-in user's traces and keeps the device settings", () => {
    const s = store({
      'now:u1:2026-10-12': '{}',
      'now:settings:u1': '{}',
      'now:focus:u1': '{}',
      'now:wallpapers:desktop': '[]',
      'money:last-account': 'a1',
      'workout.unsentSets': '[]',
      theme: 'dark',
      'hide-numbers': '1',
    });
    clearSignedInStorage(s);
    expect(s.keys()).toEqual(['hide-numbers', 'theme']);
  });

  it('survives a storage that throws or is absent', () => {
    expect(() => clearSignedInStorage(undefined)).not.toThrow();
    const broken = {
      ...store({ a: '1' }),
      key: () => {
        throw new Error('blocked');
      },
    };
    expect(() => clearSignedInStorage(broken)).not.toThrow();
  });
});
