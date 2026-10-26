// @vitest-environment jsdom
import { describe, expect, it, vi } from 'vitest';

import type { Today } from '@/lib/now/types';

import type { Ani } from './engine';
import { createToucher, SAID } from './touch';
import type { Part, Zone } from './zones';

const base: Today = {
  tasks: { late_count: 0, first_late: null, late: [], today: [], inbox_count: 0 },
  goals: [],
  focus: null,
  errors: [],
};

function setUp(today: Today = base) {
  const ani = {
    feel: vi.fn(),
    tilt: vi.fn(),
    glance: vi.fn(),
    sulk: vi.fn(),
    act: vi.fn(),
  } as unknown as Ani &
    Record<'feel' | 'tilt' | 'glance' | 'sulk' | 'act', ReturnType<typeof vi.fn>>;
  const field = document.createElement('input');
  document.body.append(field);
  let now = 0;
  let hide = false;
  const t = createToucher({
    ani: () => ani,
    today: () => today,
    focusField: () => field,
    hidden: () => hide,
    clock: () => now,
  });
  const tap = (part: Part, side: Zone['side'] = null, wait = 3000) => {
    now += wait;
    return t.tap({ part, side }, { x: 100, y: 400 })?.text;
  };
  return { ani, field, tap, hideNumbers: () => (hide = true) };
}

describe('createToucher', () => {
  it('says the focus on a pat, or asks for one and puts the cursor in the field', () => {
    const withFocus = setUp({
      ...base,
      focus: {
        date: '2026-10-26',
        kind: 'text',
        ref_id: null,
        text: 'Сдать отчёт',
        done: false,
        missing: false,
      },
    });
    expect(withFocus.tap('head')).toBe('Держу в голове: «Сдать отчёт».');
    expect(withFocus.ani.tilt).toHaveBeenCalled();

    const none = setUp();
    expect(none.tap('head')).toBe('В голове пусто. Выберем фокус дня?');
    expect(document.activeElement).toBe(none.field);
  });

  it('purrs on the third pat in a row', () => {
    const { ani, tap } = setUp();
    tap('head');
    expect(tap('head', null, 1000)).toBeUndefined();
    expect(tap('head', null, 1000)).toBe('Мурр. Ещё.');
    expect(ani.act).toHaveBeenCalledWith('idle_happy');
  });

  it('answers the belly and the legs with the day', () => {
    const { tap } = setUp({
      ...base,
      food: {
        eaten: { kcal: 1200, protein_g: 58, fat_g: 40, carbs_g: 120 },
        target: { kcal: 2000, protein_g: 120, fat_g: null, carbs_g: null },
      },
      health: {
        steps: 6120,
        active_kcal: null,
        resting_bpm: null,
        sleep: null,
        sleep_minutes: null,
        weight_kg: null,
        weight_change_7d: null,
      },
    });
    expect(tap('belly')).toBe('Ещё 62 г белка, и я довольна.');
    expect(tap('foot', 'left')?.replace(/\s/g, ' ')).toBe(
      '6 120 шагов. До десяти тысяч ещё 3 880.',
    );
  });

  it('keeps the day to herself while numbers are hidden', () => {
    const { tap, hideNumbers } = setUp({
      ...base,
      focus: {
        date: '2026-10-26',
        kind: 'text',
        ref_id: null,
        text: 'Сдать отчёт',
        done: false,
        missing: false,
      },
      habits: { done: 0, total: 1, left: ['Зарядка'], week: [] },
    });
    hideNumbers();
    expect(tap('head')).toBe('Твой фокус держу в голове.');
    expect(tap('hand', 'left')).toBe('Дай пять!');
    expect(tap('belly')).toBe('Урчит. Перекусим?');
    expect(tap('foot', 'right')).toBe('Ноги просятся гулять.');
  });

  it('gets cross, then turns her back, on off-limits taps', () => {
    const { ani, tap } = setUp();
    expect(tap('chest', 'left')).toBe('Эй, руки!');
    expect(tap('seat', 'right')).toBe('Я серьёзно.');
    expect(tap('hips')).toBe('Всё, обиделась.');
    expect(ani.sulk).toHaveBeenCalledWith(10);
    // While she sulks no tap reaches her.
    expect(tap('hand', 'left', 1000)).toBe('…');
    expect(tap('hand', 'left', 1000)).toBe('Не разговариваю.');
    expect(ani.act).not.toHaveBeenCalled();
  });

  it('forgives an off-limits tap after half a minute', () => {
    const { tap } = setUp();
    tap('chest');
    expect(tap('skirt', null, 31_000)).toBe('Эй, руки!');
  });

  it('claps a second tap on her raised hand', () => {
    const { ani, tap } = setUp({
      ...base,
      habits: { done: 1, total: 3, left: ['Зарядка', 'Вода'], week: [] },
    });
    expect(tap('hand', 'right')).toBe('Дай пять! Осталось: Зарядка.');
    expect(ani.act).toHaveBeenCalledWith('wave');
    expect(tap('hand', 'right', 1000)).toBe('Хлоп!');
  });

  it('dances on a pat, a high five and a tap on her feet in a row', () => {
    const { ani, tap } = setUp();
    tap('hair');
    tap('hand', 'left', 1500);
    expect(tap('foot', 'right', 1500)).toBe('Та-дам!');
    expect(ani.act).toHaveBeenCalledWith('spin');
  });

  it('moves her mood: kind taps up, rude ones down', () => {
    const t = createToucher({
      ani: () =>
        ({
          feel: vi.fn(),
          tilt: vi.fn(),
          glance: vi.fn(),
          sulk: vi.fn(),
          act: vi.fn(),
        }) as unknown as Ani,
      today: () => base,
      focusField: () => null,
      clock: () => 0,
    });
    expect(t.tap({ part: 'head', side: null }, { x: 0, y: 0 })?.mood).toBe(1);
    expect(t.tap({ part: 'skirt', side: null }, { x: 0, y: 0 })?.mood).toBe(-2);
    expect(t.tap({ part: 'belly', side: null }, { x: 0, y: 0 })?.mood).toBe(0);
  });

  it('gets dizzy on five taps in two seconds', () => {
    const { tap } = setUp();
    for (let i = 0; i < 4; i++) tap('shin', 'left', 300);
    expect(tap('shin', 'left', 300)).toBe('Ой, голова кружится.');
  });
});

describe('answers', () => {
  it('play a recording for a fixed line and none for a line with the day in it', () => {
    const t = createToucher({
      ani: () =>
        ({
          feel: vi.fn(),
          tilt: vi.fn(),
          glance: vi.fn(),
          sulk: vi.fn(),
          act: vi.fn(),
        }) as unknown as Ani,
      today: () => ({
        ...base,
        health: {
          steps: 4200,
          active_kcal: null,
          resting_bpm: null,
          sleep: null,
          sleep_minutes: null,
          weight_kg: null,
          weight_change_7d: null,
        },
      }),
      focusField: () => null,
      clock: () => 0,
    });
    expect(t.tap({ part: 'neck', side: null }, { x: 0, y: 0 })).toMatchObject({
      text: SAID.neck,
      recorded: 'touch/neck',
    });
    const steps = t.tap({ part: 'foot', side: 'left' }, { x: 0, y: 0 });
    expect(steps?.recorded).toBeUndefined();
    expect(steps?.silent).toBeUndefined();
  });

  it('keep every fixed line short enough for the voice line route', () => {
    for (const text of Object.values(SAID)) expect([...text].length).toBeLessThanOrEqual(200);
  });
});
