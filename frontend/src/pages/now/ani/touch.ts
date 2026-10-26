import type { Today } from '@/lib/now/types';

import type { Ani } from './engine';
import type { Zone } from './zones';

/*
 * What she does when a tap lands on her, by zone. A pat on the head and a
 * high five are sweet; her head, her belly and her legs answer with the day
 * (the focus, food, steps); the chest, the seat, the hips and the skirt are
 * off limits: she steps in, then gets cross, then turns her back for a
 * while. Five taps in two seconds make her dizzy.
 */

/** What she says back: a line to show, and a recorded one to play if any. */
export interface Answer {
  text: string;
  voice?: 'dizzy';
}

const SULK_S = 10;
// Off-limits taps are forgotten after this long (s).
const FORGIVE_S = 30;

const pick = <T>(xs: readonly T[], n: number) => xs[n % xs.length];
const grams = (g: number) => `${Math.round(g)} г`;
const steps = (n: number) => n.toLocaleString('ru-RU');

export interface Toucher {
  tap(z: Zone, at: { x: number; y: number }): Answer | null;
}

export function createToucher(
  ani: () => Ani | null,
  today: () => Today | undefined,
  focusField: () => HTMLInputElement | null,
  clock: () => number = () => performance.now(),
): Toucher {
  const taps: number[] = [];
  const count: Record<string, number> = {};
  let lastAt: Record<string, number> = {};
  let offended = 0;
  let offendedAt = -1e9;
  let sulkUntil = 0;
  let fiveAt = -1e9;

  const again = (key: string, within: number, now: number) => {
    const n = now - (lastAt[key] ?? -1e9) < within * 1000 ? (count[key] ?? 0) + 1 : 1;
    count[key] = n;
    lastAt = { ...lastAt, [key]: now };
    return n;
  };

  return {
    tap(z, at) {
      const a = ani();
      if (!a) return null;
      const now = clock();
      const day = today();

      // Sulking: she does not turn round for a while.
      if (now < sulkUntil) return { text: pick(['…', 'Не разговариваю.', 'Хм.'], taps.length) };

      taps.push(now);
      while (taps.length && now - taps[0] > 2000) taps.shift();
      if (taps.length >= 5) {
        taps.length = 0;
        a.feel('surprised', 2);
        a.act('spin');
        return { text: 'Ой, голова кружится.', voice: 'dizzy' };
      }

      switch (z.part) {
        case 'chest':
        case 'seat':
        case 'hips':
        case 'skirt': {
          if (now - offendedAt > FORGIVE_S * 1000) offended = 0;
          offended += 1;
          offendedAt = now;
          if (offended === 1) {
            a.feel('surprised', 1.6);
            a.glance(at.x, at.y, 1.6);
            return { text: 'Эй, руки!' };
          }
          if (offended === 2) {
            a.feel('cross', 3);
            return { text: 'Я серьёзно.' };
          }
          offended = 0;
          a.feel('cross', SULK_S);
          a.sulk(SULK_S);
          sulkUntil = now + SULK_S * 1000;
          return { text: 'Всё, обиделась.' };
        }

        case 'head': {
          const n = again('pat', 4, now);
          a.feel('happy', 3);
          a.tilt(at.x < innerWidth / 2 ? -1 : 1, 2.5);
          if (n === 3) {
            a.act('idle_happy');
            return { text: 'Мурр. Ещё.' };
          }
          if (n > 1) return null;
          const f = day?.focus;
          if (f?.text && !f.done) return { text: `Держу в голове: «${f.text}».` };
          if (f?.done) return { text: 'Фокус дня сделан. Я тобой горжусь.' };
          focusField()?.focus();
          return { text: 'В голове пусто. Выберем фокус дня?' };
        }

        case 'hair': {
          a.feel('cross', 1.8);
          a.glance(at.x, at.y, 1.8);
          return {
            text: pick(
              ['Эй, причёску не трогай!', 'Я её час укладывала.'],
              again('hair', 20, now) - 1,
            ),
          };
        }

        case 'face': {
          const n = again('nose', 5, now);
          if (n >= 3) {
            count.nose = 0;
            a.feel('sneeze', 0.9);
            return { text: 'Апчхи!' };
          }
          a.feel('scrunch', 1.2);
          return { text: n === 1 ? 'Ой.' : 'Хи-хи, щекотно.' };
        }

        case 'neck':
          a.feel('surprised', 1.4);
          return { text: 'Щекотно же!' };

        case 'shoulder':
          a.glance(at.x < innerWidth / 2 ? 0 : innerWidth, at.y - 120, 1.8);
          a.feel('surprised', 1.4);
          return { text: 'А? Кто там?' };

        case 'hand':
        case 'forearm': {
          // A second tap while her hand is up claps it.
          if (now - fiveAt < 2500) {
            fiveAt = -1e9;
            a.feel('happy', 2);
            return { text: 'Хлоп!' };
          }
          fiveAt = now;
          a.act('wave');
          a.feel('happy', 2.5);
          const left = day?.habits?.left ?? [];
          if (left.length) return { text: `Дай пять! Осталось: ${left[0]}.` };
          if (day?.habits && day.habits.total > 0)
            return { text: 'Дай пять! Привычки на сегодня закрыты.' };
          return { text: 'Дай пять!' };
        }

        case 'upperArm':
          a.feel('happy', 1.8);
          a.act('peace');
          return { text: 'Сильная, да?' };

        case 'belly': {
          a.feel('surprised', 1.2);
          const food = day?.food;
          const t = food?.target;
          if (food && t) {
            const left = t.protein_g - food.eaten.protein_g;
            if (left > 1) return { text: `Ещё ${grams(left)} белка, и я довольна.` };
            return { text: 'Белок на сегодня закрыт. Может, десерт?' };
          }
          if (food && food.eaten.kcal > 0)
            return { text: `Сегодня ${Math.round(food.eaten.kcal)} ккал. Урчит.` };
          return { text: 'Урчит. Еды сегодня ещё не было?' };
        }

        case 'thigh':
        case 'shin':
        case 'foot': {
          a.feel('happy', 1.8);
          const n = day?.health?.steps;
          if (n == null) return { text: 'Шагомер молчит. Пройдёмся?' };
          if (n < 10000)
            return { text: `${steps(n)} шагов. До десяти тысяч ещё ${steps(10000 - n)}.` };
          return { text: `${steps(n)} шагов! Ноги гудят.` };
        }
      }
      return null;
    },
  };
}
