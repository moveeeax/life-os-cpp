import { describe, expect, it } from 'vitest';

import { grouped, matches, noteHit, pageHits, taskHit, txHit } from './search';

describe('search', () => {
  it('matches case-insensitively, with ё as е', () => {
    expect(matches('Ёлка на Новый год', 'елка')).toBe(true);
    expect(matches('Pay rent', 'RENT')).toBe(true);
    expect(matches('Pay rent', 'tax')).toBe(false);
  });

  it('finds pages by their names', () => {
    const nav = [
      { label: 'Money', to: '/money' },
      { label: 'Notes', to: '/notes' },
    ];
    expect(pageHits('mon', nav).map((h) => h.to)).toEqual(['/money']);
    expect(pageHits('acc', nav).map((h) => h.to)).toEqual(['/account']);
  });

  it('opens a task on Today when it is due, on Later otherwise', () => {
    expect(taskHit({ id: 't1', title: 'Pay rent', due: '2026-10-30' }, '2026-10-31').to).toBe(
      '/tasks?task=t1',
    );
    expect(taskHit({ id: 't2', title: 'Visa', due: '2026-11-20' }, '2026-10-31').to).toBe(
      '/tasks/later?task=t2',
    );
    expect(taskHit({ id: 't3', title: 'Someday', due: null }, '2026-10-31').to).toBe(
      '/tasks/later?task=t3',
    );
  });

  it('a note and a ledger row open where they live', () => {
    expect(
      noteHit({ id: 'n1', title: '', date: null, updated_at: '2026-10-20T10:00:00Z' }),
    ).toEqual(
      expect.objectContaining({ title: 'Untitled note', when: '2026-10-20', to: '/notes/n1' }),
    );
    expect(txHit({ id: 'x1', name: '', merchant: 'Korzinka', date: '2026-10-29' })).toEqual(
      expect.objectContaining({ title: 'Korzinka', when: '2026-10-29', to: '/money?row=x1' }),
    );
  });

  it('groups in a fixed order and drops empty groups', () => {
    const g = grouped([
      { key: 'b', group: 'Money', title: 'x', to: '/m' },
      { key: 'a', group: 'Pages', title: 'y', to: '/p' },
    ]);
    expect(g.map(([name]) => name)).toEqual(['Pages', 'Money']);
  });
});
