import { describe, expect, it } from 'vitest';

import { daysBetween, formatDateTime, formatDay, isDay, shiftDays, todayLocal } from './dates';

// The copies these helpers replace: ledger/food/travel/tasks/health counted
// "today" from the browser's clock; Now from a named zone. The day maths of
// travel, Now's corners and its tasks panel parsed T00:00:00Z, tasks and goals
// T12:00:00Z; health and habits shifted in UTC, ledger and food on a local
// Date that fell back to today for a broken input. These tests pin what each
// caller saw.

describe('todayLocal', () => {
  it("is the browser's calendar day without a zone (ledger, food, travel, tasks, health)", () => {
    expect(todayLocal(undefined, new Date(2026, 9, 5, 23, 30))).toBe('2026-10-05');
    expect(todayLocal(undefined, new Date(2026, 0, 1, 0, 5))).toBe('2026-01-01');
  });

  it('is the day of the zone when one is given (Now)', () => {
    expect(todayLocal('Asia/Kolkata', new Date('2026-10-12T19:30:00Z'))).toBe('2026-10-13');
    expect(todayLocal('UTC', new Date('2026-10-12T19:30:00Z'))).toBe('2026-10-12');
    expect(todayLocal('America/Los_Angeles', new Date('2026-10-13T03:00:00Z'))).toBe('2026-10-12');
  });
});

describe('isDay', () => {
  it('accepts real calendar days only', () => {
    expect(isDay('2026-02-28')).toBe(true);
    expect(isDay('2028-02-29')).toBe(true);
    expect(isDay('2026-02-29')).toBe(false);
    expect(isDay('2026-13-01')).toBe(false);
    expect(isDay('2026-1-1')).toBe(false);
    expect(isDay('')).toBe(false);
  });
});

describe('shiftDays', () => {
  it('crosses months, years and leap days (every copy agreed here)', () => {
    expect(shiftDays('2026-03-01', -1)).toBe('2026-02-28');
    expect(shiftDays('2028-03-01', -1)).toBe('2028-02-29');
    expect(shiftDays('2026-12-31', 1)).toBe('2027-01-01');
    expect(shiftDays('2026-01-01', -1)).toBe('2025-12-31');
    expect(shiftDays('2026-10-05', 7)).toBe('2026-10-12');
    expect(shiftDays('2026-10-05', 0)).toBe('2026-10-05');
  });

  it('ignores daylight-saving changes', () => {
    // Europe and the US move their clocks on these weekends.
    expect(shiftDays('2026-03-28', 1)).toBe('2026-03-29');
    expect(shiftDays('2026-03-29', 1)).toBe('2026-03-30');
    expect(shiftDays('2026-10-25', -1)).toBe('2026-10-24');
    expect(shiftDays('2026-11-01', 1)).toBe('2026-11-02');
  });

  it('shifts from today for a broken input, as ledger and food did', () => {
    const today = todayLocal();
    expect(shiftDays('nope', 0)).toBe(today);
    expect(shiftDays('2026-02-30', 1)).toBe(shiftDays(today, 1));
  });
});

describe('daysBetween', () => {
  it('counts calendar days, the same for the T00:00Z and T12:00Z copies', () => {
    expect(daysBetween('2026-10-14', '2026-10-14')).toBe(0);
    expect(daysBetween('2026-10-14', '2026-10-15')).toBe(1);
    expect(daysBetween('2026-10-15', '2026-10-14')).toBe(-1);
    expect(daysBetween('2026-12-31', '2027-01-01')).toBe(1);
    expect(daysBetween('2028-02-28', '2028-03-01')).toBe(2);
    expect(daysBetween('2026-01-01', '2027-01-01')).toBe(365);
    // Across the clock changes.
    expect(daysBetween('2026-03-28', '2026-03-30')).toBe(2);
    expect(daysBetween('2026-10-24', '2026-11-02')).toBe(9);
  });

  it('is NaN for a broken day', () => {
    expect(daysBetween('nope', '2026-10-14')).toBeNaN();
  });
});

describe('formatDay', () => {
  it('ledger and food: weekday, month and day', () => {
    expect(formatDay('2026-10-05')).toBe('Mon, Oct 5');
    expect(formatDay('2026-01-01')).toBe('Thu, Jan 1');
  });

  it('travel: "Nov 1" this year, "Nov 1, 2025" another year', () => {
    expect(formatDay('2026-11-01', { yearUnless: '2026-10-14' })).toBe('Nov 1');
    expect(formatDay('2025-11-01', { yearUnless: '2026-10-14' })).toBe('Nov 1, 2025');
  });

  it('tasks and goals: day first', () => {
    expect(formatDay('2026-10-05', { locale: 'en-GB' })).toBe('5 Oct');
    expect(formatDay('2025-10-05', { locale: 'en-GB', yearUnless: '2026-10-14' })).toBe(
      '5 Oct 2025',
    );
    expect(formatDay('2026-10-05', { locale: 'en-GB', yearUnless: '2026-10-14' })).toBe('5 Oct');
  });

  it('gives a broken day back as it came', () => {
    expect(formatDay('soon')).toBe('soon');
    expect(formatDay('2026-02-30', { locale: 'en-GB' })).toBe('2026-02-30');
  });
});

describe('formatDateTime', () => {
  it("is the browser's date and time of an instant (billing pages)", () => {
    const iso = '2026-10-14T09:30:00Z';
    expect(formatDateTime(iso)).toBe(new Date(iso).toLocaleString());
  });
});
