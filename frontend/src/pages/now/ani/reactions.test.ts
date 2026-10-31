import { describe, expect, it } from 'vitest';

import { reactionTo, ReactionGate } from './reactions';

const at = (h: number, m = 0) => new Date(2026, 10, 1, h, m).getTime();

describe('reactions', () => {
  it('ReactionsToActs: a morning task, a third taxi, a closed goal, a late dinner', () => {
    const always = () => 0;
    expect(
      reactionTo({ kind: 'task_done', title: 'Отчет', at: at(9) }, { hour: 9, today: [] }, always)
        ?.text,
    ).toMatch(/Утром/);
    const taxis = [1, 2].map((i) => ({
      kind: 'money_added' as const,
      name: 'Yandex Go',
      merchant: 'Yandex Go',
      amount: -30000,
      currency: 'UZS',
      at: at(10 + i),
    }));
    expect(
      reactionTo({ ...taxis[0], at: at(15) }, { hour: 15, today: taxis }, always)?.text,
    ).toMatch(/третье/i);
    expect(
      reactionTo({ kind: 'goal_done', title: 'AWS', at: at(12) }, { hour: 12, today: [] }, always)
        ?.clip,
    ).toBe('spin');
    expect(
      reactionTo(
        { kind: 'food_added', name: 'Паста', at: at(23, 30) },
        { hour: 23, today: [] },
        always,
      )?.text,
    ).toMatch(/Поздний/);
  });

  it('a rule with a chance stays quiet when the dice say so', () => {
    expect(
      reactionTo({ kind: 'habit_ticked', at: at(9) }, { hour: 9, today: [] }, () => 0.99),
    ).toBeNull();
  });

  it('ReactionGate: one per three minutes and twelve a day', () => {
    const g = new ReactionGate();
    expect(g.allow(at(9))).toBe(true);
    expect(g.allow(at(9, 2))).toBe(false);
    expect(g.allow(at(9, 3))).toBe(true);
    let n = 2;
    for (let i = 0; i < 30; i++) if (g.allow(at(10) + i * 4 * 60_000)) n++;
    expect(n).toBe(12);
  });
});
