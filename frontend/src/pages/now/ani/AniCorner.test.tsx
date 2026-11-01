// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi, type Mock } from 'vitest';

import { api } from '@/lib/api/client';
import type { Today } from '@/lib/now/types';

import { AniCorner } from './AniCorner';
import { setSoundOn } from './chat';
import type { Ani, AniOptions } from './engine';
import type { TalkOptions } from './talk';

// The voice call: a stand-in whose connecting the test holds open and lets through.
const talkCall = {
  opts: null as TalkOptions | null,
  up: (() => {}) as () => void,
  stop: vi.fn(),
  rejectOnAbort: true,
};
vi.mock('./talk', () => ({
  startTalk: (opts: TalkOptions) =>
    new Promise((ok, fail) => {
      talkCall.opts = opts;
      opts.onState('connecting');
      opts.signal?.addEventListener('abort', () => {
        if (talkCall.rejectOnAbort) fail(new Error('aborted'));
      });
      talkCall.up = () => ok({ stop: talkCall.stop, interrupt() {}, press() {}, release() {} });
    }),
}));
vi.mock('./persona', () => ({ instructions: () => '', tools: () => [] }));

// The chat's network calls: the panel opens without a server.
vi.mock('./chat', async (orig) => ({
  ...(await orig<typeof import('./chat')>()),
  loadTurns: vi.fn(() => Promise.resolve([])),
  wakeAgent: vi.fn(() => Promise.resolve()),
}));

// Ani's server settings: the bare switch, and the bare model's signed links.
const prefs = { bare: false };
const bareLinks = {
  model_url: 'https://s3.test/private/Ani.pmx?sig',
  texture_url: 'https://s3.test/private/k.png?sig',
  expires_in: 120,
};
const fetchAniBare = vi.fn(() => Promise.resolve(bareLinks));
vi.mock('./useLife', () => ({ useLife: () => {} }));
vi.mock('./Shelf', () => ({ Shelf: () => null }));
vi.mock('./useLife2', () => ({
  // The test reaches her own lines through the say the corner hands over.
  useLife2: (o: { say: (a: unknown) => void }) => {
    (globalThis as { aniSay?: (a: unknown) => void }).aniSay = o.say;
  },
}));

vi.mock('@/hooks/useAni', () => ({
  useAniPrefs: () => ({ data: { ...prefs }, isPending: false, isSuccess: true }),
  fetchAniBare: () => fetchAniBare(),
  useMuteKind: () => () => {},
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
      cover: vi.fn(),
      blush: vi.fn(),
      hug: vi.fn(),
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

async function mount(
  today: Today = base,
  props: {
    afterFocus?: boolean;
    onFocus?: (action: 'start' | 'stop', minutes?: number) => void;
    onWallpaper?: (which?: string) => void;
    onSound?: (which?: string) => void;
    focusing?: boolean;
  } = {},
) {
  const view = render(
    <AniCorner
      base="https://s3.example.com/wallpapers/ani/"
      context={{ tz: 'Asia/Almaty', date: DATE, today }}
      {...props}
    />,
  );
  await act(async () => {
    await vi.advanceTimersByTimeAsync(10);
  });
  return view;
}

const bubble = () => document.querySelector('.ani-say')?.textContent ?? null;
const played = () => engine.ani!.say.mock.calls.map((c: unknown[]) => String(c[0]));

// Her webhook inbox: empty unless a test fills it.
const inbox = { items: [] as unknown[], waiting: 0 };
let claimSpy: ReturnType<typeof vi.spyOn>;

beforeEach(() => {
  inbox.items = [];
  inbox.waiting = 0;
  claimSpy = vi.spyOn(api, 'postJson').mockImplementation(async () => {
    const data = inbox.items;
    inbox.items = [];
    return { data };
  });
  vi.spyOn(api, 'getJson').mockImplementation(async () => ({ data: { waiting: inbox.waiting } }));
  localStorage.clear();
  localStorage.setItem('now:ani', '1');
  vi.useFakeTimers({ now: new Date(2026, 9, 26, 15, 0) });
});
afterEach(() => {
  vi.restoreAllMocks();
  document.documentElement.classList.remove('hide-numbers');
  prefs.bare = false;
  fetchAniBare.mockClear();
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

  it('GiveToAni: dropped text gets her buttons to keep it or put it in the inbox', async () => {
    await mount();
    const hit = document.querySelector('.ani-hit')!;
    const data: Record<string, string> = { 'text/plain': 'Анна любит пионы' };
    await act(async () => {
      fireEvent.drop(hit, { dataTransfer: { getData: (t: string) => data[t] ?? '' } });
      await vi.advanceTimersByTimeAsync(10);
    });
    expect(screen.getByRole('button', { name: 'Запомнить' })).toBeTruthy();
    expect(screen.getByRole('button', { name: 'Во входящие' })).toBeTruthy();
  });

  it('ReactsToActs: a closed goal on the site gets her word', async () => {
    await mount();
    // Her hello goes first; a reaction waits until her line is over.
    await act(async () => {
      await vi.advanceTimersByTimeAsync(30_000);
    });
    await act(async () => {
      window.dispatchEvent(
        new CustomEvent('life:act', {
          detail: { kind: 'goal_done', title: 'AWS', at: Date.now() },
        }),
      );
      await vi.advanceTimersByTimeAsync(10);
    });
    expect(bubble()).toMatch(/«AWS» закрыта/);
  });

  it('LineChips: her own line with buttons runs the one tapped and closes', async () => {
    await mount();
    const run = vi.fn();
    await act(async () => {
      (globalThis as { aniSay?: (a: unknown) => void }).aniSay?.({
        text: 'Угадай, сколько шагов было вчера?',
        chips: [
          { label: '4 тыс', run: () => undefined },
          { label: '7 тыс', run },
        ],
      });
      await vi.advanceTimersByTimeAsync(10);
    });
    fireEvent.click(screen.getByRole('button', { name: '7 тыс' }));
    expect(run).toHaveBeenCalledTimes(1);
    expect(screen.queryByRole('button', { name: '4 тыс' })).toBeNull();
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

  it('mounts the bare model from the given URLs', async () => {
    prefs.bare = true;
    await mount();
    fireEvent.click(screen.getByRole('button', { name: 'Outfit' }));
    fireEvent.click(screen.getByRole('button', { name: /bare/i }));
    await act(async () => {
      await vi.advanceTimersByTimeAsync(10);
    });
    expect(fetchAniBare).toHaveBeenCalledTimes(1);
    expect(engine.opts?.outfit).toBe('bare');
    expect(engine.opts?.bare).toEqual({
      model: bareLinks.model_url,
      texture: bareLinks.texture_url,
    });
    // Back in clothes: the dressed model, no links.
    fireEvent.click(screen.getByRole('button', { name: 'Outfit' }));
    fireEvent.click(screen.getByRole('button', { name: /lingerie/i }));
    await act(async () => {
      await vi.advanceTimersByTimeAsync(10);
    });
    expect(engine.opts?.bare).toBeUndefined();
  });

  it('has no Bare in the wardrobe while the switch is off', async () => {
    await mount();
    fireEvent.click(screen.getByRole('button', { name: 'Outfit' }));
    expect(screen.queryByRole('button', { name: /bare/i })).toBeNull();
    expect(screen.getByRole('button', { name: /lingerie/i })).toBeTruthy();
  });

  const hookSay = (text: string) => ({
    id: text,
    kind: 'say',
    say: text,
    payload: { text },
    created_at: new Date().toISOString(),
  });
  const tick = async (ms = 5100) =>
    act(async () => {
      await vi.advanceTimersByTimeAsync(ms);
    });

  it('plays a webhook line through her voice', async () => {
    await mount();
    await tick(20_000); // her own hello is over
    engine.ani!.say.mockClear();
    inbox.items = [hookSay('Сборка прошла.')];
    await tick();
    expect(claimSpy).toHaveBeenCalledWith('/api/v1/ani/inbox/claim');
    expect(bubble()).toBe('Сборка прошла.');
  });

  it('changes her outfit when the webhook asks', async () => {
    await mount();
    await tick(20_000);
    inbox.items = [
      {
        id: 'o',
        kind: 'outfit',
        say: null,
        payload: { outfit: 'p2' },
        created_at: new Date().toISOString(),
      },
    ];
    await tick();
    expect(engine.ani!.setOutfit).toHaveBeenCalledWith('p2');
  });

  const hookItem = (kind: string, payload: Record<string, unknown>, say: string | null = null) => ({
    id: 'x' + kind,
    kind,
    say,
    payload,
    created_at: new Date().toISOString(),
  });

  it('takes a mood from the webhook', async () => {
    await mount();
    await tick(20_000);
    inbox.items = [hookItem('mood', { level: -1, seconds: 60 })];
    await tick();
    expect(engine.ani!.mood).toHaveBeenCalledWith(-1);
  });

  it('shows a card for its seconds', async () => {
    await mount();
    await tick(20_000);
    inbox.items = [hookItem('card', { title: 'CI', text: 'зеленый', seconds: 5 })];
    await tick();
    expect(screen.getByRole('note', { name: 'CI' }).textContent).toContain('зеленый');
    await tick(6000);
    expect(screen.queryByRole('note', { name: 'CI' })).toBeNull();
  });

  it('asks with buttons and sends the answer', async () => {
    await mount();
    await tick(20_000);
    inbox.items = [hookItem('ask', { text: 'Чай?', options: ['Да', 'Нет'] }, 'Чай?')];
    await tick();
    fireEvent.click(screen.getByRole('button', { name: 'Да' }));
    expect(claimSpy).toHaveBeenCalledWith('/api/v1/ani/inbox/xask/answer', {
      body: { answer: 'Да' },
    });
    await tick(100);
    expect(screen.queryByRole('button', { name: 'Да' })).toBeNull();
  });

  it('asks one question at a time', async () => {
    await mount();
    await tick(20_000);
    inbox.items = [
      { ...hookItem('ask', { text: 'Чай?', options: ['Да', 'Нет'] }, 'Чай?'), id: 'q1' },
      { ...hookItem('ask', { text: 'Кофе?', options: ['Кофе', 'Нет'] }, 'Кофе?'), id: 'q2' },
    ];
    await tick(20_000);
    expect(screen.getByRole('button', { name: 'Да' })).toBeTruthy();
    expect(screen.queryByRole('button', { name: 'Кофе' })).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: 'Да' }));
    await tick(100);
    expect(screen.getByRole('button', { name: 'Кофе' })).toBeTruthy();
  });

  it('offers to move the open tasks to tomorrow in the evening', async () => {
    vi.setSystemTime(new Date(2026, 9, 26, 21, 30));
    const patch = vi.spyOn(api, 'patchJson').mockResolvedValue({ data: {} });
    const t: Today = {
      ...base,
      tasks: {
        late_count: 0,
        first_late: null,
        late: [],
        today: [
          { id: 'a', title: 'A', status: 'done' },
          { id: 'b', title: 'B', status: 'open' },
        ],
        inbox_count: 0,
      },
    };
    await mount(t);
    await tick(90_000);
    expect(bubble()).toBe('Сделано 1 из 2. Перенести 1 на завтра?');
    fireEvent.click(screen.getByRole('button', { name: 'Перенести' }));
    await tick(100);
    expect(patch).toHaveBeenCalledWith('/api/v1/tasks/items/b', {
      body: { due: '2026-10-27' },
      query: { tz: 'Asia/Almaty' },
    });
  });

  it('hands focus, wallpaper and sound to the page', async () => {
    const onFocus = vi.fn();
    const onWallpaper = vi.fn();
    const onSound = vi.fn();
    await mount(base, { onFocus, onWallpaper, onSound });
    await tick(20_000);
    inbox.items = [
      hookItem('focus', { action: 'start', minutes: 25 }),
      hookItem('wallpaper', {}),
      hookItem('sound', { which: 'off' }),
    ];
    await tick(10_000);
    expect(onFocus).toHaveBeenCalledWith('start', 25);
    expect(onWallpaper).toHaveBeenCalledWith(undefined);
    expect(onSound).toHaveBeenCalledWith('off');
  });

  it('claims nothing while Hide numbers is on, and shows the dot', async () => {
    document.documentElement.classList.add('hide-numbers');
    inbox.waiting = 2;
    await mount();
    await tick(20_000);
    expect(claimSpy).not.toHaveBeenCalledWith('/api/v1/ani/inbox/claim');
    expect(screen.getByRole('status', { name: /2 waiting/i })).toBeTruthy();
  });

  it('claims nothing while she is away', async () => {
    await mount();
    fireEvent.click(screen.getByRole('button', { name: 'Ani: send away' }));
    claimSpy.mockClear();
    await tick(20_000);
    expect(claimSpy).not.toHaveBeenCalledWith('/api/v1/ani/inbox/claim');
    // Her state outlives the test: back on the ledge for the next one.
    fireEvent.click(screen.getByRole('button', { name: 'Ani: call back' }));
  });

  it('keeps playing the webhook after another line cut one short', async () => {
    setSoundOn(false);
    try {
      await mount();
      await tick(20_000);
      inbox.items = [hookSay('Первая строка из вебхука.')];
      await tick();
      expect(bubble()).toBe('Первая строка из вебхука.');
      // She says something else meanwhile.
      act(() => {
        engine.opts!.onSpin?.();
      });
      expect(bubble()).toBe('Ой, голова кружится.');
      inbox.items = [hookSay('Вторая.')];
      const seen = new Set<string | null>();
      for (let i = 0; i < 40; i++) {
        await tick(500);
        seen.add(bubble());
      }
      expect(seen.has('Вторая.')).toBe(true);
    } finally {
      setSoundOn(true);
    }
  });

  const props = (over: Record<string, unknown> = {}) => ({
    base: 'https://s3.example.com/wallpapers/ani/',
    context: { tz: 'Asia/Almaty', date: DATE, today: base },
    ...over,
  });

  it('FocusInboxKept: during a focus session the webhook items wait on the server and play after it', async () => {
    const { rerender } = render(<AniCorner {...props({ focusing: true })} />);
    await tick(10);
    await tick(20_000);
    inbox.items = [hookSay('Напоминание: позвонить маме.')];
    inbox.waiting = 1;
    await tick(30_000);
    // Nothing claimed (useFocusInbox takes only the focus items), so FocusMode's count is real.
    expect(claimSpy).not.toHaveBeenCalledWith('/api/v1/ani/inbox/claim');
    expect(inbox.items).toHaveLength(1);
    rerender(<AniCorner {...props({ focusing: false })} />);
    await tick(500);
    expect(claimSpy).toHaveBeenCalledWith('/api/v1/ani/inbox/claim');
    expect(bubble()).toBe('Напоминание: позвонить маме.');
  });

  it('BreakAfterFocusOnFlip: the break line plays when the prop turns on for a mounted corner', async () => {
    const onAfterFocus = vi.fn();
    const { rerender } = render(<AniCorner {...props({ onAfterFocus })} />);
    await tick(10);
    await tick(30_000); // the hello is over
    expect(bubble()).not.toBe('Перерыв. Ты молодец, разомнись.');
    rerender(<AniCorner {...props({ onAfterFocus, afterFocus: true })} />);
    await tick(10);
    expect(bubble()).toBe('Перерыв. Ты молодец, разомнись.');
    expect(played()).toContain(
      'https://s3.example.com/wallpapers/ani/v2/voice/touch/breakTime.mp3',
    );
    expect(onAfterFocus).toHaveBeenCalledOnce();
  });

  it('HideNumbersLine: under Hide numbers a line with a number is blurred and not said aloud', async () => {
    await mount();
    await tick(30_000);
    engine.ani!.say.mockClear();
    document.documentElement.classList.add('hide-numbers');
    await act(async () => {
      (globalThis as { aniSay?: (a: unknown) => void }).aniSay?.({
        text: 'В этом месяце ушло около 345 000 KZT.',
        chips: [{ label: '4 тыс', run: () => undefined }],
      });
      await vi.advanceTimersByTimeAsync(10);
    });
    const say = document.querySelector('.ani-say')!;
    expect(say.textContent).toContain('345 000');
    expect(say.classList.contains('num')).toBe(true);
    expect(say.hasAttribute('data-num-ok')).toBe(false);
    expect(screen.getByRole('button', { name: '4 тыс' }).classList.contains('num')).toBe(true);
    expect(engine.ani!.say).not.toHaveBeenCalled();
    // The Hide numbers guard (pages/hideNumbers.guard.test.tsx) over her corner: every text
    // with a digit is blurred or says it is not personal.
    const loose: string[] = [];
    const walk = document.createTreeWalker(
      document.querySelector('.ani-corner')!,
      NodeFilter.SHOW_TEXT,
    );
    for (let n = walk.nextNode(); n; n = walk.nextNode())
      if (/\d/.test(n.textContent ?? '') && !n.parentElement?.closest('.num, [data-num-ok]'))
        loose.push(n.textContent ?? '');
    expect(loose).toEqual([]);
  });

  it('HideNumbersLine: a line without numbers is still said under Hide numbers', async () => {
    await mount();
    await tick(30_000);
    engine.ani!.say.mockClear();
    document.documentElement.classList.add('hide-numbers');
    await act(async () => {
      (globalThis as { aniSay?: (a: unknown) => void }).aniSay?.({ text: 'Пора размяться.' });
      await vi.advanceTimersByTimeAsync(10);
    });
    expect(document.querySelector('.ani-say')!.classList.contains('num')).toBe(false);
    expect(engine.ani!.say).toHaveBeenCalledOnce();
  });

  it('TalkHangUpWhileConnecting: the call is aborted and the ledge is back at once', async () => {
    await mount();
    talkCall.stop.mockClear();
    const mic = () => document.querySelector<HTMLButtonElement>('.ani-mic')!;
    await act(async () => {
      fireEvent.click(mic());
      await vi.advanceTimersByTimeAsync(10);
    });
    expect(screen.getByText('Connecting')).toBeTruthy();
    // A second tap is the hang-up.
    await act(async () => {
      fireEvent.click(mic());
      await vi.advanceTimersByTimeAsync(10);
    });
    expect(talkCall.opts?.signal?.aborted).toBe(true);
    expect(screen.queryByText('Connecting')).toBeNull();
    expect(screen.getByText('With you')).toBeTruthy();
    // A late state from the dead call changes nothing, and no error line shows.
    act(() => talkCall.opts?.onState('listening'));
    expect(screen.queryByText('Listening')).toBeNull();
    expect(bubble() ?? '').not.toMatch(/Не получилось подключиться/);
  });

  it('TalkHangUpWhileConnecting: a call that comes up after the hang-up is stopped', async () => {
    await mount();
    talkCall.stop.mockClear();
    const mic = () => document.querySelector<HTMLButtonElement>('.ani-mic')!;
    await act(async () => {
      fireEvent.click(mic());
      await vi.advanceTimersByTimeAsync(10);
    });
    // The stand-in ignores the abort here: it answers as a call that came up just then.
    talkCall.rejectOnAbort = false;
    try {
      await act(async () => {
        fireEvent.click(mic());
        talkCall.up();
        await vi.advanceTimersByTimeAsync(10);
      });
    } finally {
      talkCall.rejectOnAbort = true;
    }
    expect(talkCall.stop).toHaveBeenCalled();
    expect(screen.getByText('With you')).toBeTruthy();
  });

  it('RefreshAfterGive: a task she moved on a drop refreshes Tasks and Now', async () => {
    const qc = new QueryClient();
    const invalidate = vi.spyOn(qc, 'invalidateQueries');
    vi.spyOn(api, 'patchJson').mockResolvedValue({ data: {} });
    render(
      <QueryClientProvider client={qc}>
        <AniCorner {...props()} />
      </QueryClientProvider>,
    );
    await tick(10);
    await tick(30_000);
    const hit = document.querySelector('.ani-hit')!;
    const data: Record<string, string> = {
      'application/x-life-give': JSON.stringify({ kind: 'task', id: 't1', title: 'Отчет' }),
    };
    await act(async () => {
      fireEvent.drop(hit, { dataTransfer: { getData: (t: string) => data[t] ?? '' } });
      await vi.advanceTimersByTimeAsync(10);
    });
    await act(async () => {
      fireEvent.click(screen.getByRole('button', { name: 'Сегодня в 19:00' }));
      await vi.advanceTimersByTimeAsync(10);
    });
    const keys = invalidate.mock.calls.map((c) => JSON.stringify(c[0]?.queryKey));
    expect(keys).toContain(JSON.stringify(['tasks']));
    expect(keys).toContain(JSON.stringify(['now']));
  });
});
