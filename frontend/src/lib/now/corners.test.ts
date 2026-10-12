import { describe, expect, it } from 'vitest';

import { chips, countdownFigure, countdowns, formatMoney, formatSleep } from './corners';
import type { Today } from './types';

const base: Today = {
  tasks: { late_count: 0, first_late: null, late: [], today: [], inbox_count: 0 },
  goals: [],
  focus: null,
  errors: [],
};

const text = (parts: { text: string }[]) => parts.map((p) => p.text).join('');
const at = (iso: string) => new Date(iso);

describe('formatSleep', () => {
  it('writes hours and minutes', () => {
    expect(formatSleep(412)).toBe('6 h 52');
    expect(formatSleep(60)).toBe('1 h 00');
    expect(formatSleep(45)).toBe('45 min');
  });
});

describe('chips', () => {
  it('always gives four corners in order', () => {
    expect(chips(base, 10, '2026-10-12').map((c) => c.key)).toEqual([
      'body',
      'goals',
      'day',
      'tasks',
    ]);
  });

  it('says so when the band has nothing for today', () => {
    const body = chips(base, 10, '2026-10-12')[0];
    expect(text(body.main)).toBe('No data from the band today');
    expect(body.small).toEqual([]);
    expect(body.dot).toBeNull();
  });

  it('writes sleep, steps and the weekly weight change', () => {
    const t: Today = {
      ...base,
      health: {
        steps: 6120,
        active_kcal: 370,
        resting_bpm: 58,
        sleep: { minutes: 412, start: null, end: '2026-10-12T01:58:00Z', stages: [] },
        sleep_minutes: 412,
        weight_kg: 91.6,
        weight_change_7d: -0.4,
      },
    };
    const body = chips(t, 10, '2026-10-12')[0];
    expect(text(body.main)).toBe('6 h 52 sleep · 6,120 steps');
    expect(text(body.small)).toBe('91.6 kg, −0.4 this week');
    expect(body.main.some((p) => p.num)).toBe(true);
  });

  it('drops the missing parts of Body', () => {
    const t: Today = {
      ...base,
      health: {
        steps: null,
        active_kcal: null,
        resting_bpm: null,
        sleep: null,
        sleep_minutes: null,
        weight_kg: 91.6,
        weight_change_7d: null,
      },
    };
    const body = chips(t, 10, '2026-10-12')[0];
    expect(text(body.main)).toBe('91.6 kg');
    expect(body.small).toEqual([]);
  });

  it('names the nearest countdown and a goal behind, with a dot', () => {
    const t: Today = {
      ...base,
      goals: [
        {
          id: 'w',
          title: 'Weight 75 kg',
          kind: 'number',
          due: '2027-03-31',
          pace: 'behind',
          progress: 0.08,
          days_left: 170,
        },
        {
          id: 'a',
          title: 'AWS exam',
          kind: 'binary',
          due: '2026-10-31',
          pace: 'on_track',
          progress: null,
          days_left: 19,
        },
      ],
    };
    const g = chips(t, 10, '2026-10-12')[1];
    expect(text(g.main)).toBe('AWS exam in 19 days');
    expect(text(g.small)).toBe('Weight 75 kg is behind');
    expect(g.dot).toBe('warn');
  });

  it('is calm about goals when nothing is due soon and nothing is behind', () => {
    const g = chips(base, 10, '2026-10-12')[1];
    expect(text(g.main)).toBe('No goal due soon');
    expect(text(g.small)).toBe('All on track');
    expect(g.dot).toBeNull();
  });

  it('writes protein left and the first currency spent, keeping currencies apart', () => {
    const t: Today = {
      ...base,
      food: {
        eaten: { kcal: 1480, protein_g: 80, fat_g: 47, carbs_g: 161 },
        target: { kcal: 2200, protein_g: 142, fat_g: 73, carbs_g: 230 },
      },
      money: {
        currencies: [
          { currency: 'KZT', today: 7500, projection: 300000, median3: 350000 },
          { currency: 'USD', today: 12, projection: null, median3: null },
        ],
        spends_today: [],
        net_worth: null,
      },
    };
    const d = chips(t, 20, '2026-10-12')[2];
    expect(text(d.main)).toBe('62 g protein left');
    expect(text(d.small)).toBe('7,500 KZT spent today');
    expect(d.dot).toBe('warn');
    expect(chips(t, 10, '2026-10-12')[2].dot).toBeNull();
  });

  it('falls back to kcal and to nothing spent', () => {
    const t: Today = {
      ...base,
      food: { eaten: { kcal: 1480, protein_g: 0, fat_g: 0, carbs_g: 0 }, target: null },
    };
    const d = chips(t, 10, '2026-10-12')[2];
    expect(text(d.main)).toBe('1,480 kcal eaten');
    expect(text(d.small)).toBe('Nothing spent today');
  });

  it('counts today and late, names the next task, and marks late in red', () => {
    const t: Today = {
      ...base,
      tasks: {
        late_count: 1,
        first_late: { id: 'l', title: 'Renew the insurance', due: '2026-10-09' },
        late: [{ id: 'l', title: 'Renew the insurance', due: '2026-10-09' }],
        today: [
          { id: 'a', title: 'Lab: hybrid DNS', status: 'done' },
          { id: 'b', title: 'Call the bank', status: 'open' },
          { id: 'c', title: 'Check in', status: 'open' },
        ],
        inbox_count: 3,
      },
    };
    const k = chips(t, 10, '2026-10-12')[3];
    expect(text(k.main)).toBe('2 today · 1 late');
    expect(text(k.small)).toBe('Next: Call the bank');
    expect(k.dot).toBe('late');
  });

  it('says nothing is late when the day is clear', () => {
    const k = chips(base, 10, '2026-10-12')[3];
    expect(text(k.main)).toBe('Nothing for today');
    expect(text(k.small)).toBe('Nothing late');
  });
});

describe('countdowns', () => {
  const goals: Today['goals'] = [
    {
      id: '1',
      title: 'Exam',
      kind: 'binary',
      due: '2026-10-31',
      pace: 'on_track',
      progress: null,
      days_left: 19,
    },
    {
      id: '2',
      title: 'Books',
      kind: 'count',
      due: '2026-12-31',
      pace: 'on_track',
      progress: 0.58,
      days_left: 80,
    },
    {
      id: '3',
      title: 'Course',
      kind: 'steps',
      due: '2026-10-13',
      pace: 'behind',
      progress: 0.3,
      days_left: 1,
    },
    {
      id: '4',
      title: 'Today',
      kind: 'binary',
      due: '2026-10-12',
      pace: 'on_track',
      progress: null,
      days_left: 0,
    },
    {
      id: '5',
      title: 'Past',
      kind: 'count',
      due: '2026-10-01',
      pace: 'behind',
      progress: 0.1,
      days_left: 0,
    },
  ];
  it('lists goals due within 60 days, nearest first, past ones left out', () => {
    const list = countdowns({ ...base, goals }, '2026-10-12');
    expect(list.map((c) => c.id)).toEqual(['4', '3', '1']);
    expect(list.map((c) => c.words)).toEqual(['today', 'tomorrow', 'in 19 days']);
  });
  it('takes a wider horizon when asked', () => {
    expect(countdowns({ ...base, goals }, '2026-10-12', 90).map((c) => c.id)).toEqual([
      '4',
      '3',
      '1',
      '2',
    ]);
  });
  it('uses the local date, not the instant', () => {
    expect(at('2026-10-12T00:00:00Z').getTime()).toBeGreaterThan(0);
  });
});

describe('formatMoney', () => {
  it('keeps the cents when there are any', () => {
    expect(formatMoney(4.5)).toBe('4.50');
    expect(formatMoney(2790)).toBe('2,790');
    expect(formatMoney(13275.61)).toBe('13,275.61');
  });
});

describe('countdownFigure', () => {
  it('shows a number only from two days on', () => {
    expect(countdownFigure(0)).toEqual({ figure: null, words: 'today' });
    expect(countdownFigure(1)).toEqual({ figure: null, words: 'tomorrow' });
    expect(countdownFigure(19)).toEqual({ figure: '19', words: 'days left' });
  });
});

describe('the Food and money chip with cents', () => {
  it('writes 4.50 USD, not 5', () => {
    const t: Today = {
      ...base,
      money: {
        currencies: [{ currency: 'USD', today: 4.5, projection: null, median3: null }],
        spends_today: [],
        net_worth: null,
      },
    };
    expect(text(chips(t, 10, '2026-10-12')[2].small)).toBe('4.50 USD spent today');
  });
});

describe('the Food and money chip with all accounts', () => {
  it('shows all accounts when nothing was spent today', () => {
    const t: Today = {
      ...base,
      money: {
        currencies: [],
        spends_today: [],
        net_worth: {
          currency: 'USD',
          total: 23456.7,
          partial: false,
          rate_date: '2026-10-13',
          change_30d: 312,
          change_partial: false,
        },
      },
    };
    expect(text(chips(t, 10, '2026-10-13')[2].small)).toBe('All accounts 23,457 USD');
  });
  it('says when the total leaves money out', () => {
    const t: Today = {
      ...base,
      money: {
        currencies: [],
        spends_today: [],
        net_worth: {
          currency: 'USD',
          total: 23456.7,
          partial: true,
          rate_date: '2026-10-13',
          change_30d: 312,
          change_partial: true,
        },
      },
    };
    expect(text(chips(t, 10, '2026-10-13')[2].small)).toBe('All accounts 23,457 USD · partial');
  });
  it('keeps the spend line when something was spent', () => {
    const t: Today = {
      ...base,
      money: {
        currencies: [{ currency: 'KZT', today: 7500, projection: null, median3: null }],
        spends_today: [],
        net_worth: {
          currency: 'USD',
          total: 23456.7,
          partial: false,
          rate_date: '2026-10-13',
          change_30d: 312,
          change_partial: false,
        },
      },
    };
    expect(text(chips(t, 10, '2026-10-13')[2].small)).toBe('7,500 KZT spent today');
  });
});
