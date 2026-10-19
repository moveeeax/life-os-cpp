import { describe, expect, it } from 'vitest';

import { isoWeekday, repeatDraft, repeatLabel, repeatOf, sameRepeat } from './repeat';

describe('repeat rules', () => {
  it('reads the weekday of a day the ISO way', () => {
    expect(isoWeekday('2026-10-19')).toBe(1);
    expect(isoWeekday('2026-10-25')).toBe(7);
  });

  it('starts the form from the rule, or from the due date', () => {
    expect(repeatDraft(null, '2026-10-22')).toEqual({
      kind: '',
      weekdays: [4],
      day: '22',
      every: '2',
    });
    expect(repeatDraft({ kind: 'weekly', weekdays: [1, 5] }, '2026-10-22').weekdays).toEqual([
      1, 5,
    ]);
    expect(repeatDraft({ kind: 'monthly', day: 31 }).day).toBe('31');
  });

  it('turns the form into a rule the API takes, or says it is not one yet', () => {
    const d = repeatDraft(null);
    expect(repeatOf(d)).toBeNull();
    expect(repeatOf({ ...d, kind: 'daily' })).toEqual({ kind: 'daily' });
    expect(repeatOf({ ...d, kind: 'weekly', weekdays: [5, 1, 5] })).toEqual({
      kind: 'weekly',
      weekdays: [1, 5],
    });
    expect(repeatOf({ ...d, kind: 'weekly', weekdays: [] })).toBeUndefined();
    expect(repeatOf({ ...d, kind: 'monthly', day: '32' })).toBeUndefined();
    expect(repeatOf({ ...d, kind: 'monthly', day: '15' })).toEqual({ kind: 'monthly', day: 15 });
    expect(repeatOf({ ...d, kind: 'every_n_days', every: '1.5' })).toBeUndefined();
    expect(repeatOf({ ...d, kind: 'every_n_days', every: '3' })).toEqual({
      kind: 'every_n_days',
      every: 3,
    });
  });

  it('compares and names rules', () => {
    expect(sameRepeat(null, undefined)).toBe(true);
    expect(sameRepeat({ kind: 'daily' }, { kind: 'daily' })).toBe(true);
    expect(sameRepeat({ kind: 'monthly', day: 1 }, { kind: 'monthly', day: 2 })).toBe(false);
    expect(repeatLabel(null)).toBeNull();
    expect(repeatLabel({ kind: 'weekly', weekdays: [1, 4] })).toBe('every Mon, Thu');
    expect(repeatLabel({ kind: 'monthly', day: 31 })).toBe('monthly on the 31st');
    expect(repeatLabel({ kind: 'monthly', day: 12 })).toBe('monthly on the 12th');
    expect(repeatLabel({ kind: 'monthly', day: 22 })).toBe('monthly on the 22nd');
    expect(repeatLabel({ kind: 'every_n_days', every: 3 })).toBe('every 3 days');
  });
});
