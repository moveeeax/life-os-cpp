// @vitest-environment jsdom
import { describe, expect, it, vi } from 'vitest';

import type { Today } from '@/lib/now/types';

import type { Ani } from './engine';
import { createToucher } from './touch';
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
  const t = createToucher(
    () => ani,
    () => today,
    () => field,
    () => now,
  );
  const tap = (part: Part, side: Zone['side'] = null, wait = 3000) => {
    now += wait;
    return t.tap({ part, side }, { x: 100, y: 400 })?.text;
  };
  return { ani, field, tap };
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

  it('gets cross, then turns her back, on off-limits taps', () => {
    const { ani, tap } = setUp();
    expect(tap('chest', 'left')).toBe('Эй, руки!');
    expect(tap('seat', 'right')).toBe('Я серьёзно.');
    expect(tap('hips')).toBe('Всё, обиделась.');
    expect(ani.sulk).toHaveBeenCalledWith(10);
    // While she sulks no tap reaches her.
    expect(['…', 'Не разговариваю.', 'Хм.']).toContain(tap('hand', 'left', 1000));
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

  it('gets dizzy on five taps in two seconds', () => {
    const { tap } = setUp();
    for (let i = 0; i < 4; i++) tap('shin', 'left', 300);
    expect(tap('shin', 'left', 300)).toBe('Ой, голова кружится.');
  });
});
