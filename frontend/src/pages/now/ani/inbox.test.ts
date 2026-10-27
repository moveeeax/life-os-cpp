// @vitest-environment jsdom
import { act, renderHook } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { api } from '@/lib/api/client';

import { planBacklog, POLL_MS, useInbox, type InboxItem } from './inbox';

const NOW = Date.parse('2026-10-27T12:00:00Z');
const at = (minutesAgo: number) => new Date(NOW - minutesAgo * 60_000).toISOString();
let n = 0;
const item = (over: Partial<InboxItem>): InboxItem => ({
  id: `i${++n}`,
  kind: 'say',
  say: 'Привет',
  payload: {},
  created_at: at(0),
  ...over,
});

describe('planBacklog', () => {
  it('plays lines, moves and outfits in order', () => {
    expect(
      planBacklog(
        [
          item({ say: 'Сборка прошла.' }),
          item({ kind: 'act', say: null, payload: { name: 'wave' } }),
          item({ kind: 'outfit', say: null, payload: { outfit: 'p2' } }),
          item({ kind: 'event', say: 'Маша зовёт.' }),
        ],
        NOW,
      ),
    ).toEqual([
      { kind: 'say', text: 'Сборка прошла.' },
      { kind: 'act', name: 'wave' },
      { kind: 'outfit', outfit: 'p2' },
      { kind: 'say', text: 'Маша зовёт.' },
    ]);
  });

  it('skips moves older than 10 minutes', () => {
    expect(
      planBacklog(
        [
          item({ kind: 'act', say: null, payload: { name: 'wave' }, created_at: at(11) }),
          item({ kind: 'act', say: null, payload: { name: 'peace' }, created_at: at(9) }),
        ],
        NOW,
      ),
    ).toEqual([{ kind: 'act', name: 'peace' }]);
  });

  it('keeps only the newest outfit', () => {
    expect(
      planBacklog(
        [
          item({ kind: 'outfit', say: null, payload: { outfit: 'p1' }, created_at: at(30) }),
          item({ kind: 'outfit', say: null, payload: { outfit: 'lingerie' }, created_at: at(20) }),
          item({ say: 'Ок' }),
        ],
        NOW,
      ),
    ).toEqual([
      { kind: 'outfit', outfit: 'lingerie' },
      { kind: 'say', text: 'Ок' },
    ]);
  });

  it('opens a backlog of more than 3 lines with a greeting', () => {
    const four = [1, 2, 3, 4].map((i) => item({ say: `строка ${i}` }));
    const plan = planBacklog(four, NOW);
    expect(plan[0]).toEqual({ kind: 'say', text: 'Пока тебя не было, пришло 4 сообщения' });
    expect(plan).toHaveLength(5);
    const five = [1, 2, 3, 4, 5].map((i) => item({ say: `${i}` }));
    expect(planBacklog(five, NOW)[0]).toEqual({
      kind: 'say',
      text: 'Пока тебя не было, пришло 5 сообщений',
    });
    expect(planBacklog(four.slice(0, 3), NOW)[0]).toEqual({ kind: 'say', text: 'строка 1' });
  });

  it('drops an event without a line and an unknown outfit', () => {
    expect(
      planBacklog(
        [
          item({ kind: 'event', say: null }),
          item({ kind: 'outfit', say: null, payload: { outfit: 'cape' } }),
        ],
        NOW,
      ),
    ).toEqual([]);
  });
});

describe('useInbox', () => {
  beforeEach(() => {
    vi.useFakeTimers({ now: NOW });
  });
  afterEach(() => {
    vi.useRealTimers();
    vi.restoreAllMocks();
  });

  it('claims every 5 s while enabled and plays one at a time', async () => {
    const claim = vi
      .spyOn(api, 'postJson')
      .mockResolvedValueOnce({ data: [item({ say: 'Раз' }), item({ say: 'Два' })] })
      .mockResolvedValue({ data: [] });
    const played: string[] = [];
    const play = vi.fn(async (p: { kind: string; text?: string }) => {
      played.push(p.text ?? p.kind);
    });
    renderHook(() => useInbox(true, play));
    await act(async () => {
      await vi.advanceTimersByTimeAsync(POLL_MS + 10);
    });
    expect(claim).toHaveBeenCalledWith('/api/v1/ani/inbox/claim');
    await act(async () => {
      await vi.advanceTimersByTimeAsync(5000);
    });
    expect(played).toEqual(['Раз', 'Два']);
  });

  it('only counts while held, for the dot', async () => {
    const claim = vi.spyOn(api, 'postJson');
    vi.spyOn(api, 'getJson').mockResolvedValue({ data: { waiting: 2 } });
    const { result } = renderHook(() => useInbox(false, vi.fn()));
    await act(async () => {
      await vi.advanceTimersByTimeAsync(POLL_MS + 10);
    });
    expect(claim).not.toHaveBeenCalled();
    expect(result.current.waiting).toBe(2);
  });
});
