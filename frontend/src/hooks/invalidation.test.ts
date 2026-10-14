import { describe, expect, it } from 'vitest';

import { qk } from '@/lib/api/queryKeys';

import { GOAL_WRITES } from './useGoals';
import { FOOD_DIARY_WRITES } from './useFood';
import { HABIT_WRITES } from './useHabits';
import { MONEY_WRITES } from './useMoney';

// Now's digest (useToday) carries goals, food and money: a write in a section
// must refresh it too, or Now shows the old numbers for up to a minute.
describe('section writes refresh Now', () => {
  const now = JSON.stringify(qk.now.all());
  for (const [name, keys] of [
    ['goals', GOAL_WRITES],
    ['food diary', FOOD_DIARY_WRITES],
    ['money', MONEY_WRITES],
    ['habits', HABIT_WRITES],
  ] as const) {
    it(name, () => {
      expect(keys.map((k) => JSON.stringify(k))).toContain(now);
    });
  }
});
