// @vitest-environment jsdom
import { act, renderHook } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { api } from '@/lib/api/client';

import { planBacklog, POLL_MS, useFocusInbox, useInbox, type InboxItem } from './inbox';

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

describe('planBacklog, the newer kinds', () => {
  it('plays reminders and questions as lines, and the rest as they are', () => {
    expect(
      planBacklog(
        [
          item({ kind: 'remind', say: 'Встань' }),
          item({
            id: 'q1',
            kind: 'ask',
            say: 'Чай?',
            payload: { text: 'Чай?', options: ['Да', 'Нет'] },
          }),
          item({ kind: 'mood', say: null, payload: { level: 1, seconds: 600 } }),
          item({ kind: 'card', say: null, payload: { title: 'CI', text: 'ok', seconds: 20 } }),
          item({ kind: 'focus', say: null, payload: { action: 'start', minutes: 25 } }),
          item({ kind: 'wallpaper', say: null, payload: {} }),
          item({ kind: 'sound', say: null, payload: { which: 'off' } }),
          item({ kind: 'hold', say: null, payload: { minutes: 30 } }),
        ],
        NOW,
      ),
    ).toEqual([
      { kind: 'say', text: 'Встань', line: 'reminder' },
      { kind: 'ask', id: 'q1', text: 'Чай?', options: ['Да', 'Нет'] },
      { kind: 'mood', level: 1, seconds: 600 },
      { kind: 'card', title: 'CI', text: 'ok', url: undefined, seconds: 20 },
      { kind: 'focus', action: 'start', minutes: 25 },
      { kind: 'wallpaper', which: undefined },
      { kind: 'sound', which: 'off' },
    ]);
  });

  it('keeps only the newest mood, focus, wallpaper and sound, and drops stale cards', () => {
    const plan = planBacklog(
      [
        item({ kind: 'mood', say: null, payload: { level: -1, seconds: 600 } }),
        item({
          kind: 'card',
          say: null,
          payload: { title: 'old', seconds: 20 },
          created_at: at(11),
        }),
        item({ kind: 'mood', say: null, payload: { level: 1, seconds: 600 } }),
        item({ kind: 'focus', say: null, payload: { action: 'start', minutes: 25 } }),
        item({ kind: 'focus', say: null, payload: { action: 'stop' } }),
      ],
      NOW,
    );
    expect(plan).toEqual([
      { kind: 'mood', level: 1, seconds: 600 },
      { kind: 'focus', action: 'stop', minutes: undefined },
    ]);
  });
});

describe('planBacklog, stale page actions', () => {
  it('drops a mood, focus, wallpaper or sound older than 10 minutes', () => {
    const old = at(11);
    expect(
      planBacklog(
        [
          item({ kind: 'mood', say: null, payload: { level: 1, seconds: 600 }, created_at: old }),
          item({
            kind: 'focus',
            say: null,
            payload: { action: 'start', minutes: 25 },
            created_at: old,
          }),
          item({ kind: 'wallpaper', say: null, payload: {}, created_at: old }),
          item({ kind: 'sound', say: null, payload: { which: 'off' }, created_at: old }),
        ],
        NOW,
      ),
    ).toEqual([]);
  });
});

describe('useFocusInbox', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(NOW);
  });
  afterEach(() => {
    vi.useRealTimers();
    vi.restoreAllMocks();
  });

  it('takes only focus items while a session runs, and applies the newest', async () => {
    const post = vi.spyOn(api, 'postJson').mockResolvedValue({
      data: [
        item({ kind: 'focus', say: null, payload: { action: 'start', minutes: 50 } }),
        item({ kind: 'focus', say: null, payload: { action: 'stop' } }),
      ],
    });
    const onFocus = vi.fn();
    renderHook(() => useFocusInbox(true, onFocus));
    await act(async () => {
      await vi.advanceTimersByTimeAsync(10);
    });
    expect(post).toHaveBeenCalledWith('/api/v1/ani/inbox/claim?kinds=focus');
    expect(onFocus).toHaveBeenCalledTimes(1);
    expect(onFocus).toHaveBeenCalledWith('stop', undefined);
  });

  it('does nothing while idle', async () => {
    const post = vi.spyOn(api, 'postJson').mockResolvedValue({ data: [] });
    renderHook(() => useFocusInbox(false, vi.fn()));
    await act(async () => {
      await vi.advanceTimersByTimeAsync(POLL_MS * 2);
    });
    expect(post).not.toHaveBeenCalled();
  });
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
      { kind: 'say', text: 'Сборка прошла.', line: 'webhook' },
      { kind: 'act', name: 'wave' },
      { kind: 'outfit', outfit: 'p2' },
      { kind: 'say', text: 'Маша зовёт.', line: 'webhook' },
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
      { kind: 'say', text: 'Ок', line: 'webhook' },
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
    expect(planBacklog(four.slice(0, 3), NOW)[0]).toEqual({
      kind: 'say',
      text: 'строка 1',
      line: 'webhook',
    });
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

  it('keeps the rest of a batch while held and plays it after', async () => {
    vi.spyOn(api, 'postJson')
      .mockResolvedValueOnce({
        data: [item({ say: 'Раз' }), item({ say: 'Два' }), item({ say: 'Три' })],
      })
      .mockResolvedValue({ data: [] });
    vi.spyOn(api, 'getJson').mockResolvedValue({ data: { waiting: 0 } });
    const played: string[] = [];
    const play = vi.fn(async (p: { kind: string; text?: string }) => {
      played.push(p.text ?? p.kind);
      await new Promise((ok) => setTimeout(ok, 1000));
    });
    const { rerender } = renderHook(({ on }) => useInbox(on, play), { initialProps: { on: true } });
    await act(async () => {
      await vi.advanceTimersByTimeAsync(500);
    });
    expect(played).toEqual(['Раз']);
    rerender({ on: false });
    await act(async () => {
      await vi.advanceTimersByTimeAsync(10_000);
    });
    expect(played).toEqual(['Раз']);
    rerender({ on: true });
    await act(async () => {
      await vi.advanceTimersByTimeAsync(10_000);
    });
    expect(played).toEqual(['Раз', 'Два', 'Три']);
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

describe('planBacklog turn', () => {
  it('a spoken answer names its turn and an event from Telegram its kind', () => {
    const p = planBacklog(
      [item({ kind: 'event', say: 'Анна пишет', payload: { source: 'telegram', turn: 't9' } })],
      NOW,
    );
    expect(p[0]).toEqual({ kind: 'say', text: 'Анна пишет', turn: 't9', line: 'telegram' });
  });
});
