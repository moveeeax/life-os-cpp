import type { Act } from '@/lib/now/act';

/**
 * Ani's instant reactions to what the person does on the site (lib/now/act):
 * a rules table, no agent. Each rule says when it fits and how likely it is,
 * so she does not answer everything like a machine; the gate keeps her to
 * one reaction per three minutes and twelve a day.
 */

export type Reaction = {
  text: string;
  clip?: 'peace' | 'spin' | 'wave';
  feel?: 'happy' | 'surprised' | 'shy';
};

type Ctx = { hour: number; today: Act[] };

const TAXI = /yandex go|uber|bolt|taxi|такси|grab|careem|mytaxi/i;
const pick = <T>(xs: T[], r: number) => xs[Math.floor(r * xs.length) % xs.length];

type Rule = {
  when: (a: Act, c: Ctx) => boolean;
  chance: number;
  say: (a: Act, c: Ctx, r: number) => Reaction;
};

const RULES: Rule[] = [
  {
    when: (a) => a.kind === 'goal_done',
    chance: 1,
    say: (a) => ({
      text: `«${(a as { title: string }).title}» закрыта! Это было большое.`,
      clip: 'spin',
      feel: 'happy',
    }),
  },
  {
    when: (a, c) =>
      a.kind === 'money_added' &&
      TAXI.test(`${a.name} ${a.merchant ?? ''}`) &&
      c.today.filter((x) => x.kind === 'money_added' && TAXI.test(`${x.name} ${x.merchant ?? ''}`))
        .length >= 2,
    chance: 1,
    say: () => ({ text: 'Опять такси? Это уже третье за сегодня.', feel: 'surprised' }),
  },
  {
    when: (a, c) => a.kind === 'task_done' && c.hour < 10,
    chance: 0.8,
    say: () => ({ text: 'Утром закрыта задача, день задался.', clip: 'peace', feel: 'happy' }),
  },
  {
    when: (a, c) =>
      a.kind === 'task_done' && c.today.filter((x) => x.kind === 'task_done').length >= 4,
    chance: 0.7,
    say: (_a, c) => ({
      text: `Уже ${c.today.filter((x) => x.kind === 'task_done').length + 1} задач за сегодня. Продуктивно!`,
      clip: 'peace',
    }),
  },
  {
    when: (a) => a.kind === 'task_done',
    chance: 0.3,
    say: (_a, _c, r) => ({
      text: pick(['Готово!', 'Одной меньше.', 'Вычеркнули.', 'Так держать.'], r),
      feel: 'happy',
    }),
  },
  {
    when: (a, c) => a.kind === 'food_added' && c.hour >= 23,
    chance: 0.9,
    say: () => ({ text: 'Поздний ужин. Записала, без осуждения.', feel: 'shy' }),
  },
  {
    when: (a) => a.kind === 'habit_ticked',
    chance: 0.4,
    say: (_a, _c, r) => ({
      text: pick(['Отметка есть.', 'Привычка держится.', 'Еще один день в серию.'], r),
      clip: 'peace',
    }),
  },
  {
    when: (a) => a.kind === 'workout_started',
    chance: 0.9,
    say: () => ({ text: 'Тренировка! Удачи, я подожду.', clip: 'wave' }),
  },
  {
    when: (a, c) => a.kind === 'money_added' && a.amount > 0 && c.hour < 24,
    chance: 0.8,
    say: () => ({ text: 'Поступление, приятно.', feel: 'happy' }),
  },
  {
    when: (a) => a.kind === 'note_opened',
    chance: 0.15,
    say: (a) => ({ text: `«${(a as { title: string }).title}». Устроить по ней квиз потом?` }),
  },
];

/** Her reaction to @p a, or null; @p roll gives numbers in [0, 1) (Math.random outside tests). */
export function reactionTo(a: Act, c: Ctx, roll: () => number = Math.random): Reaction | null {
  for (const rule of RULES) {
    if (!rule.when(a, c)) continue;
    return roll() < rule.chance ? rule.say(a, c, roll()) : null;
  }
  return null;
}

const GAP_MS = 3 * 60_000;
const PER_DAY = 12;

/** One reaction per three minutes, twelve a day (by the local day of each). */
export class ReactionGate {
  private last = -Infinity;
  private day = '';
  private count = 0;
  allow(now: number): boolean {
    const d = new Date(now).toDateString();
    if (d !== this.day) {
      this.day = d;
      this.count = 0;
    }
    if (now - this.last < GAP_MS || this.count >= PER_DAY) return false;
    this.last = now;
    this.count += 1;
    return true;
  }
}
