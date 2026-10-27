import type { Today } from '@/lib/now/types';

import type { Ani } from './engine';
import type { Zone } from './zones';

/*
 * What she does when a tap lands on her, by zone. A pat on the head and a
 * high five are sweet; her head, her belly and her legs answer with the day
 * (the focus, food, steps); the chest, the seat, the hips and the skirt are
 * off limits: she steps in, then gets cross, then turns her back for a
 * while. Five taps in two seconds make her dizzy, and a pat, a high five
 * and a tap on her feet in a row (within six seconds) make her dance.
 *
 * Each answer carries what it does to her mood of the day (mood.ts): kind
 * taps lift it, off-limits ones and pulled hair bring it down.
 */

/**
 * What she says back. A fixed line is recorded ahead under
 * v2/voice/<recorded>.mp3; a line with the day in it has no recording and is
 * read aloud by the server (GET /api/v1/now/voice-line); a silent one is
 * only shown.
 */
export interface Answer {
  text: string;
  recorded?: string;
  silent?: boolean;
  /** Her mood moved by this tap (up kind, down rude). */
  mood?: number;
  /** A tap on the bubble opens the chat with her agent. */
  chat?: boolean;
}

/** Her fixed lines, recorded ahead (OpenAI TTS, marin) as v2/voice/touch/<id>.mp3. */
export const SAID = {
  hands: 'Эй, руки!',
  serious: 'Я серьёзно.',
  offended: 'Всё, обиделась.',
  silent: 'Не разговариваю.',
  hm: 'Хм.',
  purr: 'Мурр. Ещё.',
  focusDone: 'Фокус дня сделан. Я тобой горжусь.',
  focusNone: 'В голове пусто. Выберем фокус дня?',
  hair: 'Эй, причёску не трогай!',
  hairDone: 'Я её час укладывала.',
  oh: 'Ой.',
  giggle: 'Хи-хи, щекотно.',
  sneeze: 'Апчхи!',
  neck: 'Щекотно же!',
  who: 'А? Кто там?',
  clap: 'Хлоп!',
  five: 'Дай пять!',
  fiveDone: 'Дай пять! Привычки на сегодня закрыты.',
  strong: 'Сильная, да?',
  proteinDone: 'Белок на сегодня закрыт. Может, десерт?',
  hungry: 'Урчит. Еды сегодня ещё не было?',
  noSteps: 'Шагомер молчит. Пройдёмся?',
  // In place of the day's data while numbers are hidden (a shared screen).
  focusKept: 'Твой фокус держу в голове.',
  tummy: 'Урчит. Перекусим?',
  walk: 'Ноги просятся гулять.',
  // Not taps: the focus ticked, a focus session run to its end.
  focusTicked: 'Есть! Фокус сделан.',
  breakTime: 'Перерыв. Ты молодец, разомнись.',
  // Not taps either: an hour without a break, the evening, coming back.
  stretch: 'Ты уже час за компом. Разомнёмся?',
  focusOpen: 'Фокус сегодня не закрыт. Может, ещё успеешь?',
  dayDone: 'Фокус сделан. Хороший был день.',
  howWasDay: 'Расскажешь, как прошёл день?',
  grumpyHello: 'Хм. Привет.',
  missed: 'Снова ты! Я скучала.',
  // The combo: a pat, a high five, her feet.
  dance: 'Та-дам!',
} as const;
export type Said = keyof typeof SAID;

/** One of her fixed lines, with its recording. */
export const say = (id: Said): Answer => ({ text: SAID[id], recorded: `touch/${id}` });

const SULK_S = 10;
// Off-limits taps are forgotten after this long (s).
const FORGIVE_S = 30;

const pick = <T>(xs: readonly T[], n: number) => xs[n % xs.length];
const grams = (g: number) => `${Math.round(g)} г`;
const steps = (n: number) => n.toLocaleString('ru-RU');

export interface Toucher {
  tap(z: Zone, at: { x: number; y: number }): Answer | null;
  /** The cursor rested on a part of her: bare, she catches the look. */
  stare(z: Zone, at: { x: number; y: number }): Answer | null;
}

// A look caught at most this often (ms), and the lines for it in turn.
const STARE_EVERY = 25_000;
const STARE = ['Глаза выше.', 'Я всё вижу.', 'Эй, я тут, повыше.'];

export interface ToucherOptions {
  ani: () => Ani | null;
  today: () => Today | undefined;
  /** The focus field, focused when she asks for a focus. */
  focusField: () => HTMLInputElement | null;
  /** "Hide numbers" is on: she keeps the day's data to herself. */
  hidden?: () => boolean;
  /** She wears nothing (the bare outfit): shy rather than cross. */
  bare?: () => boolean;
  clock?: () => number;
}

export function createToucher({
  ani,
  today,
  focusField,
  hidden = () => false,
  bare = () => false,
  clock = () => performance.now(),
}: ToucherOptions): Toucher {
  const taps: number[] = [];
  const count: Record<string, number> = {};
  let lastAt: Record<string, number> = {};
  let offended = 0;
  let offendedAt = -1e9;
  let sulkUntil = 0;
  let fiveAt = -1e9;
  let coverUntil = -1e9;
  let staredAt = -1e9;
  let stares = 0;

  const again = (key: string, within: number, now: number) => {
    const n = now - (lastAt[key] ?? -1e9) < within * 1000 ? (count[key] ?? 0) + 1 : 1;
    count[key] = n;
    lastAt = { ...lastAt, [key]: now };
    return n;
  };

  // The parts of the last taps, for the combo.
  let combo: { part: string; at: number }[] = [];
  const COMBO = ['head', 'hand', 'foot'];
  const groupOf = (p: Zone['part']) =>
    p === 'head' || p === 'hair'
      ? 'head'
      : p === 'hand' || p === 'forearm'
        ? 'hand'
        : p === 'foot' || p === 'shin'
          ? 'foot'
          : p;
  // What a tap on a part does to her mood, unless the answer says otherwise.
  const MOOD: Partial<Record<Zone['part'], number>> = {
    head: 1,
    hand: 1,
    forearm: 1,
    upperArm: 1,
    hair: -1,
    chest: -2,
    seat: -2,
    hips: -2,
    skirt: -2,
  };

  return {
    tap(z, at) {
      const r = react(z, at);
      return r && r.mood === undefined ? { ...r, mood: MOOD[z.part] ?? 0 } : r;
    },
    stare(z, at) {
      const a = ani();
      const now = clock();
      if (!a || !bare() || now < sulkUntil || now - staredAt < STARE_EVERY) return null;
      if (z.part !== 'chest' && z.part !== 'hips' && z.part !== 'seat') return null;
      staredAt = now;
      coverUntil = now + 2000;
      a.feel('squint', 2.5);
      a.glance(at.x, at.y, 2);
      a.cover(2);
      a.blush(4);
      return { text: STARE[stares++ % STARE.length], mood: 0 };
    },
  };

  function react(z: Zone, at: { x: number; y: number }): Answer | null {
    const a = ani();
    if (!a) return null;
    const now = clock();
    const day = today();

    // Sulking: she does not turn round for a while.
    if (now < sulkUntil) {
      const n = (count.sulk = (count.sulk ?? 0) + 1);
      return n % 3 === 1 ? { text: '…', silent: true } : say(n % 3 === 2 ? 'silent' : 'hm');
    }

    taps.push(now);
    while (taps.length && now - taps[0] > 2000) taps.shift();
    if (taps.length >= 5) {
      taps.length = 0;
      a.feel('surprised', 2);
      a.act('spin');
      combo = [];
      return { text: 'Ой, голова кружится.', recorded: 'dizzy', mood: -1 };
    }

    combo = [...combo, { part: groupOf(z.part), at: now }]
      .slice(-3)
      .filter((c) => now - c.at < 6000);
    if (combo.map((c) => c.part).join() === COMBO.join()) {
      combo = [];
      a.feel('happy', 3);
      a.act('spin');
      return { ...say('dance'), mood: 2 };
    }

    // Covering herself: a tap on the arm over her body counts as on the body.
    const part =
      now < coverUntil && (z.part === 'hand' || z.part === 'forearm' || z.part === 'upperArm')
        ? 'chest'
        : z.part;
    switch (part) {
      case 'chest':
      case 'seat':
      case 'hips':
      case 'skirt': {
        if (now - offendedAt > FORGIVE_S * 1000) offended = 0;
        offended += 1;
        offendedAt = now;
        if (bare()) {
          // Bare: she covers herself and goes pink; the third time she
          // turns away for a while.
          a.blush(offended < 3 ? 8 : SULK_S);
          coverUntil = now + (offended === 1 ? 3 : offended === 2 ? 4 : SULK_S) * 1000;
          if (offended === 1) {
            a.feel('shy', 3);
            a.cover(3);
            return { text: 'Ой! Я же без всего…', mood: -1 };
          }
          if (offended === 2) {
            a.feel('shy', 4);
            a.cover(4);
            a.glance(at.x, at.y, 1.5);
            return { text: 'Не смотри так. Смущаешь.', mood: -1 };
          }
          offended = 0;
          a.feel('shy', SULK_S);
          a.cover(SULK_S);
          a.sulk(SULK_S);
          sulkUntil = now + SULK_S * 1000;
          return { text: 'Всё, отвернусь. Сам виноват.', mood: -1 };
        }
        if (offended === 1) {
          a.feel('surprised', 1.6);
          a.glance(at.x, at.y, 1.6);
          return say('hands');
        }
        if (offended === 2) {
          a.feel('cross', 3);
          return say('serious');
        }
        offended = 0;
        a.feel('cross', SULK_S);
        a.sulk(SULK_S);
        sulkUntil = now + SULK_S * 1000;
        return say('offended');
      }

      case 'head': {
        const n = again('pat', 4, now);
        a.feel('happy', 3);
        a.tilt(at.x < innerWidth / 2 ? -1 : 1, 2.5);
        if (n === 3) {
          a.act('idle_happy');
          return say('purr');
        }
        if (n > 1) return null;
        const f = day?.focus;
        if (f?.text && !f.done)
          return hidden() ? say('focusKept') : { text: `Держу в голове: «${f.text}».` };
        if (f?.done) return say('focusDone');
        focusField()?.focus();
        return say('focusNone');
      }

      case 'hair': {
        a.feel('cross', 1.8);
        a.glance(at.x, at.y, 1.8);
        return say(pick(['hair', 'hairDone'] as const, again('hair', 20, now) - 1));
      }

      case 'face': {
        const n = again('nose', 5, now);
        if (n >= 3) {
          count.nose = 0;
          a.feel('sneeze', 0.9);
          return say('sneeze');
        }
        a.feel('scrunch', 1.2);
        return say(n === 1 ? 'oh' : 'giggle');
      }

      case 'neck':
        a.feel('surprised', 1.4);
        return say('neck');

      case 'shoulder':
        a.glance(at.x < innerWidth / 2 ? 0 : innerWidth, at.y - 120, 1.8);
        a.feel('surprised', 1.4);
        return say('who');

      case 'hand':
      case 'forearm': {
        // A second tap while her hand is up claps it.
        if (now - fiveAt < 2500) {
          fiveAt = -1e9;
          a.feel('happy', 2);
          return say('clap');
        }
        fiveAt = now;
        a.act('wave');
        a.feel('happy', 2.5);
        const left = day?.habits?.left ?? [];
        if (left.length)
          return hidden() ? say('five') : { text: `Дай пять! Осталось: ${left[0]}.` };
        if (day?.habits && day.habits.total > 0) return say('fiveDone');
        return say('five');
      }

      case 'upperArm':
        a.feel('happy', 1.8);
        a.act('peace');
        return say('strong');

      case 'belly': {
        a.feel('surprised', 1.2);
        if (hidden()) return say('tummy');
        const food = day?.food;
        const t = food?.target;
        if (food && t) {
          const left = t.protein_g - food.eaten.protein_g;
          if (left > 1) return { text: `Ещё ${grams(left)} белка, и я довольна.` };
          return say('proteinDone');
        }
        if (food && food.eaten.kcal > 0)
          return { text: `Сегодня ${Math.round(food.eaten.kcal)} ккал. Урчит.` };
        return say('hungry');
      }

      case 'thigh':
      case 'shin':
      case 'foot': {
        a.feel('happy', 1.8);
        if (hidden()) return say('walk');
        const n = day?.health?.steps;
        if (n == null) return say('noSteps');
        if (n < 10000)
          return { text: `${steps(n)} шагов. До десяти тысяч ещё ${steps(10000 - n)}.` };
        return { text: `${steps(n)} шагов! Ноги гудят.` };
      }
    }
    return null;
  }
}
