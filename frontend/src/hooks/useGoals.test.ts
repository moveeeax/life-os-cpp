import { afterEach, describe, expect, it, vi } from 'vitest';

/**
 * The goals routes count a goal's default day and its source days in the
 * caller's zone: the list, the detail, a new goal and an edit all send tz.
 */

const { getJson, postJson, patchJson } = vi.hoisted(() => ({
  getJson: vi.fn(async () => ({ data: [] })),
  postJson: vi.fn(async () => ({ data: {} })),
  patchJson: vi.fn(async () => ({ data: {} })),
}));
vi.mock('@/lib/api/client', async () => {
  const actual = await vi.importActual<typeof import('@/lib/api/client')>('@/lib/api/client');
  return { ...actual, api: { ...actual.api, getJson, postJson, patchJson } };
});

import { fetchGoal, fetchGoals, saveGoal } from './useGoals';

afterEach(() => vi.clearAllMocks());

const TZ = 'Asia/Almaty';

describe('the goals routes get the zone', () => {
  it('on the list and the detail', async () => {
    await fetchGoals('active', '2026-10-20', TZ);
    expect(getJson).toHaveBeenLastCalledWith('/api/v1/goals', {
      query: { status: 'active', date: '2026-10-20', tz: TZ },
      signal: undefined,
    });
    await fetchGoal('g1', '2026-10-20', TZ);
    expect(getJson).toHaveBeenLastCalledWith('/api/v1/goals/g1', {
      query: { date: '2026-10-20', tz: TZ },
      signal: undefined,
    });
  });

  it('on a new goal and on an edit', async () => {
    const body = {
      title: 'Run',
      area: 'health' as const,
      kind: 'binary' as const,
      due: '2026-12-31',
    };
    await saveGoal(null, body, TZ);
    expect(postJson).toHaveBeenLastCalledWith('/api/v1/goals', { query: { tz: TZ }, body });
    await saveGoal('g1', { source: null }, TZ);
    expect(patchJson).toHaveBeenLastCalledWith('/api/v1/goals/g1', {
      query: { tz: TZ },
      body: { source: null },
    });
  });
});
