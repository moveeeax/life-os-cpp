// @vitest-environment jsdom
import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi, type Mock } from 'vitest';

import type { Today } from '@/lib/now/types';

import { AniCorner } from './AniCorner';
import { setSoundOn } from './chat';
import type { Ani, AniOptions } from './engine';

// The chat's network calls: the panel opens without a server.
vi.mock('./chat', async (orig) => ({
  ...(await orig<typeof import('./chat')>()),
  loadTurns: vi.fn(() => Promise.resolve([])),
  wakeAgent: vi.fn(() => Promise.resolve()),
}));

// The engine draws with WebGL; here it is a stand-in that is ready at once
// and remembers what the corner asked of it.
const engine = {
  opts: null as AniOptions | null,
  ani: null as Record<keyof Ani, Mock> | null,
};
vi.mock('./engine', () => ({
  mountAni: (_canvas: HTMLCanvasElement, opts: AniOptions) => {
    engine.opts = opts;
    engine.ani = {
      present: vi.fn((_on: boolean, done?: () => void) => done?.()),
      setOutfit: vi.fn(),
      say: vi.fn(() => Promise.resolve()),
      follow: vi.fn(),
      dispose: vi.fn(),
      feel: vi.fn(),
      tilt: vi.fn(),
      glance: vi.fn(),
      sulk: vi.fn(),
      act: vi.fn(),
      mood: vi.fn(),
      hush: vi.fn(),
    };
    setTimeout(() => opts.onReady?.(), 0);
    return engine.ani as unknown as Ani;
  },
}));

const base: Today = {
  tasks: { late_count: 0, first_late: null, late: [], today: [], inbox_count: 0 },
  goals: [],
  focus: null,
  errors: [],
};
const DATE = '2026-10-26';
const late: Today = {
  ...base,
  tasks: { late_count: 1, first_late: null, late: [], today: [], inbox_count: 0 },
};

async function mount(today: Today = base, props: { afterFocus?: boolean } = {}) {
  render(
    <AniCorner
      base="https://s3.example.com/wallpapers/ani/"
      context={{ tz: 'Asia/Almaty', date: DATE, today }}
      {...props}
    />,
  );
  await act(async () => {
    await vi.advanceTimersByTimeAsync(10);
  });
}

const bubble = () => document.querySelector('.ani-say')?.textContent ?? null;
const played = () => engine.ani!.say.mock.calls.map((c: unknown[]) => String(c[0]));

beforeEach(() => {
  localStorage.clear();
  localStorage.setItem('now:ani', '1');
  vi.useFakeTimers({ now: new Date(2026, 9, 26, 15, 0) });
});
afterEach(() => {
  cleanup();
  vi.useRealTimers();
  engine.opts = null;
  engine.ani = null;
});

describe('AniCorner', () => {
  it('says the first hello of the day with a thing from it, once a day', async () => {
    await mount(late);
    expect(bubble()).toBe('Привет! Одна задача просрочена, глянешь?');
    expect(played()[0]).toContain('/api/v1/now/voice-line?text=');
    cleanup();
    await mount(late);
    expect(bubble()).toBeNull();
  });

  it('sends the user on a break after a focus session that ran to its end', async () => {
    const onAfterFocus = vi.fn();
    render(
      <AniCorner
        base="https://s3.example.com/wallpapers/ani/"
        context={{ tz: 'Asia/Almaty', date: DATE, today: base }}
        afterFocus
        onAfterFocus={onAfterFocus}
      />,
    );
    await act(async () => {
      await vi.advanceTimersByTimeAsync(10);
    });
    expect(bubble()).toBe('Перерыв. Ты молодец, разомнись.');
    expect(played()[0]).toBe('https://s3.example.com/wallpapers/ani/v2/voice/touch/breakTime.mp3');
    expect(onAfterFocus).toHaveBeenCalledOnce();
  });

  it('cheers when the focus is ticked while she watches', async () => {
    const focus = {
      date: DATE,
      kind: 'text' as const,
      ref_id: null,
      text: 'Отчёт',
      done: false,
      missing: false,
    };
    const { rerender } = render(
      <AniCorner
        base="https://s3.example.com/wallpapers/ani/"
        context={{ tz: 'Asia/Almaty', date: DATE, today: { ...base, focus } }}
      />,
    );
    await act(async () => {
      await vi.advanceTimersByTimeAsync(10);
    });
    rerender(
      <AniCorner
        base="https://s3.example.com/wallpapers/ani/"
        context={{
          tz: 'Asia/Almaty',
          date: DATE,
          today: { ...base, focus: { ...focus, done: true } },
        }}
      />,
    );
    expect(bubble()).toBe('Есть! Фокус сделан.');
    expect(engine.ani!.act).toHaveBeenCalledWith('peace');
  });

  it('suggests a break after an hour of activity', async () => {
    await mount();
    // Hello said; an hour of moving the mouse, a little every minute.
    for (let i = 0; i < 62; i++) {
      fireEvent.pointerMove(window);
      await act(async () => {
        await vi.advanceTimersByTimeAsync(60_000);
      });
    }
    expect(played()).toContain('https://s3.example.com/wallpapers/ani/v2/voice/touch/stretch.mp3');
  });

  it('asks about the day in the evening, and the bubble opens the chat', async () => {
    vi.setSystemTime(new Date(2026, 9, 26, 21, 30));
    await mount();
    await act(async () => {
      await vi.advanceTimersByTimeAsync(90_000);
    });
    expect(bubble()).toBe('Расскажешь, как прошёл день?');
    fireEvent.click(screen.getByRole('button', { name: 'Расскажешь, как прошёл день?' }));
    expect(engine.ani!.hush).toHaveBeenCalled();
    expect(document.querySelector('.ani-chat')).not.toBeNull();
  });

  it('opens the chat on a double tap', async () => {
    await mount();
    act(() => engine.opts!.onDoubleTap?.());
    expect(document.querySelector('.ani-chat')).not.toBeNull();
  });

  it('only shows her lines while she is muted', async () => {
    setSoundOn(false);
    try {
      await mount(late);
      expect(bubble()).toBe('Привет! Одна задача просрочена, глянешь?');
      expect(played()).toEqual([]);
    } finally {
      setSoundOn(true);
    }
  });
});
