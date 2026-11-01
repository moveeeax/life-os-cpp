import { useQuery } from '@tanstack/react-query';
import { useEffect, useRef, useState } from 'react';
import { useLocation } from 'react-router';

import { useHabitDays } from '@/hooks/useHabits';
import { useMoneySettings } from '@/hooks/useMoney';
import { useCrossings } from '@/hooks/useTravel';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import { clockIn } from '@/lib/dates';
import { CURRENCY_OF } from '@/lib/now/currency';

import type { AniContext } from './AniCorner';
import type { Ani } from './engine';
import {
  bedtimeLine,
  bedtimeOf,
  birthdayToday,
  brokenStreakOf,
  followUpOf,
  guessOptions,
  MEAL_HOURS,
  mealOptions,
  moodLow,
  nightOf,
  questionOfDay,
  sleepRecord,
  stepsRecord,
  tabTip,
  tripHello,
  nextSpendGroup,
  unusualSpend,
  windDownNow,
  wishDue,
  type Meal,
} from './life2';
import { arrival } from '../modes/modes';
import { useWarm } from './warm';
import { addMoment, MORNING_FROM } from './life';
import type { Changed } from './refresh';
import type { Answer } from './touch';

const TICK_MS = 60_000;
const KEY = 'now:ani:life2';
const DAY_MS = 86_400_000;
const shift = (date: string, by: number) =>
  new Date(Date.parse(`${date}T00:00:00Z`) + by * DAY_MS).toISOString().slice(0, 10);

function read(): Record<string, string> {
  try {
    return JSON.parse(localStorage.getItem(KEY) ?? '{}') as Record<string, string>;
  } catch {
    return {};
  }
}
function mark(k: string, v: string) {
  try {
    localStorage.setItem(KEY, JSON.stringify({ ...read(), [k]: v }));
  } catch {
    // At worst a line comes once more.
  }
}

/** Hide numbers is on: her lines that carry a personal number wait until it is off. */
const numbersHidden = () => document.documentElement.classList.contains('hide-numbers');

let reads = 0;

/** The warm-up step of the next read (warm.ts): one after another, not one burst at load. */
function useStep(): number {
  const [step] = useState(() => reads++ % 16);
  return step;
}

/**
 * One GET of the site for her lines: quiet when the module is off (404) or the call fails.
 * @p key is the endpoint's key wherever the site reads it, so a write there reaches her too.
 */
function useRead<T>(
  key: readonly unknown[],
  path: string,
  query: Record<string, string | number>,
  on: boolean,
  stale = 10 * 60_000,
) {
  // Each read takes its own step after the page has settled (warm.ts), not one burst at load.
  const warm = useWarm(on, 3 + useStep());
  return useQuery({
    queryKey: key,
    enabled: warm,
    retry: false,
    staleTime: stale,
    queryFn: async ({ signal }) => (await api.getJson<{ data: T }>(path, { query, signal })).data,
  });
}

type Sleep = { start_at: string; end_at: string; duration_minutes: number; is_nap: boolean };
type Item = {
  id: string;
  name: string;
  kcal: number;
  protein_g: number;
  servings: { grams: number }[];
};
type Tx = {
  id: string;
  type: string;
  amount: number;
  currency: string;
  category_id: string | null;
  name?: string | null;
  merchant?: string | null;
  note?: string;
};

/**
 * Her second set of own lines: the question of the day, follow-ups and wishes
 * from her facts, the mood mirror, bedtime, a lighter habit, records, a meal
 * at each meal time, an unusual spend, a hello abroad, the steps guess, the
 * birthday, Telegram at noon and a word on the open tab. Same rules as
 * useLife: one line per tick, each once, never over something else.
 */
export function useLife2({
  on,
  context,
  engine,
  quiet,
  say,
  onSeed,
  onChanged,
}: {
  on: boolean;
  context: AniContext;
  engine: React.RefObject<Ani | null>;
  quiet: () => boolean;
  say: (a: Answer) => void;
  onSeed: (text: string | null) => void;
  /** One of her chips changed something (a meal, a note on a spend): the page reads it again. */
  onChanged?: (what: Changed) => void;
}) {
  const { tz, date } = context;
  const { pathname } = useLocation();
  const facts = useRead<{ id: string; text: string }[]>(
    qk.ani.facts(),
    '/api/v1/ani/facts',
    {},
    on,
  );
  const mood = useRead<{ date: string; score: number }[]>(
    ['ani', 'life', 'mood', date],
    '/api/v1/habits/mood',
    { from: date, to: date },
    on,
  );
  const sleeps = useRead<Sleep[]>(
    ['ani', 'life', 'sleep', date],
    '/api/v1/fitness/sleep',
    { from: shift(date, -30), to: date },
    on,
  );
  // The Habits section's own read: a tick there reaches her streak line.
  const habits = useHabitDays(shift(date, -5), date, useWarm(on, 3 + useStep()));
  const steps = useRead<{ date: string; value: number | null }[]>(
    ['ani', 'life', 'steps', date],
    '/api/v1/metrics/series',
    { source: 'fitness:steps', from: shift(date, -30), to: date, tz },
    on,
  );
  const food = useRead<{
    totals: { kcal: number; protein_g: number };
    targets: { kcal: number; protein_g: number } | null;
  }>(['ani', 'life', 'food-day', date], '/api/v1/food/day', { date, tz }, on, 60_000);
  const recent = useRead<Item[]>(['ani', 'life', 'food-recent'], '/api/v1/food/recent', {}, on);
  const goalsProfile = useRead<{ profile: { birth_date: string | null } }>(
    ['ani', 'life', 'food-goals'],
    '/api/v1/food/goals',
    {},
    on,
  );
  const waiting = useRead<{ title: string }[]>(
    qk.telegramWaiting(),
    '/api/v1/telegram/waiting',
    {},
    on,
  );
  const spends = useRead<Tx[]>(
    ['ani', 'life', 'tx-today', date],
    '/api/v1/money/transactions',
    { from: date, to: date, status: 'posted', limit: 20 },
    on,
    5 * 60_000,
  );
  const crossings = useCrossings();
  const view = useMoneySettings().data?.view_currency ?? 'KZT';
  const arrived = arrival(crossings.data, date);
  const local = arrived ? CURRENCY_OF[arrived.to_country] : undefined;
  // The trip mode card's key: one read of the rate for both.
  const rate = useRead<{ amount: number }>(
    qk.now.arrivalRate(local ?? '', view, date),
    '/api/v1/money/rates/convert',
    { amount: 1, from: local ?? '', to: view, date },
    on && !!local && local !== view,
    60 * 60_000,
  );
  const report = useRead<{ as_if?: { expense: number; currency: string } | null }>(
    ['ani', 'life', 'report', date],
    '/api/v1/money/reports/period',
    { kind: 'month', date },
    on,
  );
  const goals = useRead<{ title: string; progress?: { pace?: string } | null }[]>(
    ['ani', 'life', 'goals', date],
    '/api/v1/goals',
    { status: 'active', date, tz },
    on,
  );

  const snapshot = () => ({
    context,
    pathname,
    facts: facts.data,
    mood: mood.data,
    sleeps: sleeps.data,
    habits: habits.data?.data,
    steps: steps.data,
    food: food.data,
    recent: recent.data,
    birth: goalsProfile.data?.profile?.birth_date ?? null,
    waiting: waiting.data,
    spends: spends.data,
    arrived,
    local,
    view,
    rate: rate.data?.amount ?? null,
    spent: report.data?.as_if
      ? { amount: report.data.as_if.expense, currency: report.data.as_if.currency }
      : null,
    stalled:
      (Array.isArray(goals.data) ? goals.data : []).find((g) => g.progress?.pace === 'behind')
        ?.title ?? null,
    // Whether each read has answered: a line is marked as said only once its data is in, or a
    // line checked before its read (the reads warm up one by one) would be lost for the day.
    got: {
      sleeps: sleeps.isSuccess,
      steps: steps.isSuccess,
      habits: habits.isSuccess,
      waiting: waiting.isSuccess,
      report: report.isSuccess,
      goals: goals.isSuccess,
    },
  });
  const live = useRef(snapshot());
  live.current = snapshot();
  const sayRef = useRef(say);
  sayRef.current = say;
  const changedRef = useRef(onChanged);
  changedRef.current = onChanged;
  const quietRef = useRef(quiet);
  quietRef.current = quiet;
  const seedRef = useRef(onSeed);
  seedRef.current = onSeed;

  useEffect(() => {
    if (!on) return;
    const since = Date.now();
    const tick = async () => {
      if (quietRef.current()) return;
      const d = live.current;
      const day = d.context.date;
      const seen = read();
      const once = (k: string) => seen[k] !== day;
      const age = (Date.now() - since) / 1000;
      const { hour, minute } = clockIn(d.context.tz);
      const morning = hour >= MORNING_FROM && hour < 12;
      // Lines with a personal number in them keep quiet under Hide numbers (not marked: they
      // may come once it is off).
      const hidden = numbersHidden();
      const e = engine.current;
      const arr = <T>(x: T[] | undefined) => (Array.isArray(x) ? x : []);

      if (birthdayToday(d.birth, day) && once('birthday')) {
        mark('birthday', day);
        addMoment(day, 'День рождения');
        e?.feel('happy', 5);
        e?.act('spin');
        sayRef.current({
          text: 'С днем рождения! Сегодня все задачи подождут, а я рядом весь день.',
          mood: 1,
          kind: 'hello',
        });
        return;
      }
      const low = moodLow(arr(d.mood), day);
      if (low && once('mood')) {
        mark('mood', day);
        sayRef.current({
          text: 'Сегодня без спешки. Может, десять минут пройтись? Я подожду.',
          mood: 0,
          kind: 'hello',
        });
        return;
      }
      if (d.arrived && once(`trip:${d.arrived.to_country}`)) {
        // Under Hide numbers the hello goes without the price.
        const line = tripHello(d.arrived.to_country, d.local, d.view, hidden ? null : d.rate);
        if (line) {
          mark(`trip:${d.arrived.to_country}`, day);
          e?.act('wave');
          sayRef.current({ text: line, kind: 'hello' });
          return;
        }
      }
      if (morning && age >= 60 && d.got.sleeps && !hidden && once('bedtimeMorning')) {
        const nights = arr(d.sleeps).filter((s) => !s.is_nap);
        const last = nights.reduce<Sleep | undefined>(
          (a, b) => (!a || b.end_at > a.end_at ? b : a),
          undefined,
        );
        const line = bedtimeLine(
          last,
          bedtimeOf(
            nights.filter((s) => s !== last),
            d.context.tz,
          ),
          d.context.tz,
        );
        mark('bedtimeMorning', day);
        if (line) {
          sayRef.current({ text: line, kind: 'brief' });
          return;
        }
      }
      if (morning && age >= 120 && d.got.sleeps && d.got.steps && !hidden && once('records')) {
        mark('records', day);
        const line = sleepRecord(arr(d.sleeps)) ?? stepsRecord(arr(d.steps), day);
        if (line) {
          addMoment(day, line);
          e?.feel('happy', 3);
          e?.act('peace');
          sayRef.current({ text: line, mood: 1, kind: 'hello' });
          return;
        }
      }
      if (morning && age >= 180 && d.got.habits && once('streak')) {
        mark('streak', day);
        const name = brokenStreakOf(arr(d.habits), day);
        if (name) {
          sayRef.current({
            text: `Вчера «${name}» не вышло. Сегодня можно легче: пять минут, просто чтобы не терять нитку.`,
            kind: 'hello',
          });
          return;
        }
      }
      if (hour >= 8 && hour < 12 && age >= 240 && d.got.steps && !hidden && once('guess')) {
        const y = arr(d.steps).find((p) => p.date === shift(day, -1))?.value;
        mark('guess', day);
        if (y != null && y > 0) {
          const opts = guessOptions(y, day);
          const real = new Intl.NumberFormat('ru-RU').format(Math.round(y));
          sayRef.current({
            text: 'Угадай, сколько шагов было вчера?',
            kind: 'hello',
            chips: opts.map((o) => ({
              label: o.label,
              run: () => {
                if (o.right) e?.act('peace');
                sayRef.current({
                  text: o.right ? `Точно! ${real} шагов.` : `Почти. Было ${real}.`,
                  mood: o.right ? 1 : 0,
                  kind: 'hello',
                });
              },
            })),
          });
          return;
        }
      }
      if (age >= 300 && hour >= 10 && hour < 20 && once('question')) {
        mark('question', day);
        const q = questionOfDay(day);
        seedRef.current(`Вопрос дня «${q}». Мой ответ: `);
        sayRef.current({
          text: `Вопрос дня: ${q} Нажми, ответишь в чате.`,
          chat: true,
          kind: 'hello',
        });
        return;
      }
      const follow = followUpOf(arr(d.facts), day);
      if (follow && age >= 360 && once(`follow:${follow.id}`)) {
        mark(`follow:${follow.id}`, day);
        seedRef.current(`Про «${follow.what}»: `);
        sayRef.current({
          text: `Вчера было «${follow.what}». Как прошло? Нажми и расскажи.`,
          chat: true,
          kind: 'hello',
        });
        return;
      }
      const wish = wishDue(arr(d.facts), day);
      if (wish && age >= 420 && once(`wish:${wish.id}`)) {
        mark(`wish:${wish.id}`, '9999');
        seedRef.current(`Про «${wish.what}»: `);
        sayRef.current({
          text: `Месяц назад была мысль: ${wish.what}. Еще хочется? Нажми и скажи.`,
          chat: true,
          kind: 'hello',
        });
        return;
      }
      if (hour === 12 && d.got.waiting && once('tgNoon')) {
        mark('tgNoon', day);
        const w = arr(d.waiting);
        if (w.length > 0) {
          sayRef.current({
            text: hidden
              ? `В Telegram ждут ответа, главный ${w[0].title}.`
              : `Ждут ответа ${w.length}: главный ${w[0].title}.`,
            kind: 'telegram',
          });
          return;
        }
      }
      for (const [meal, at] of Object.entries(MEAL_HOURS) as [Meal, number][]) {
        if (hour !== at || hidden || !once(`meal:${meal}`) || !d.food?.targets) continue;
        mark(`meal:${meal}`, day);
        const left = {
          kcal: d.food.targets.kcal - d.food.totals.kcal,
          protein_g: d.food.targets.protein_g - d.food.totals.protein_g,
        };
        if (left.protein_g <= 5) continue;
        const opts = mealOptions(left, arr(d.recent));
        if (opts.length === 0) continue;
        sayRef.current({
          text: `До цели ${Math.round(left.protein_g)} г белка. Что-то из этого?`,
          kind: 'hello',
          chips: opts.map((o) => ({
            label: `${o.item.name} ${Math.round(o.grams)} г`,
            run: () =>
              void api
                .postJson('/api/v1/food/entries', {
                  body: { date: day, meal, item_id: o.item.id, grams: o.grams },
                })
                .then(() => {
                  changedRef.current?.('food');
                  sayRef.current({
                    text: `Записала: ${o.item.name}.`,
                    silent: true,
                    kind: 'hello',
                  });
                })
                .catch(() => undefined),
          })),
        });
        return;
      }
      // One history read a tick, for every spend of one category and currency.
      const group = nextSpendGroup(arr(d.spends), (id) => once(`spend:${id}`));
      if (group) {
        for (const t of group.items) mark(`spend:${t.id}`, '9999');
        try {
          const hist = (
            await api.getJson<{ data: Tx[] }>('/api/v1/money/transactions', {
              query: {
                category: group.category,
                currency: group.currency,
                from: shift(day, -90),
                to: shift(day, -1),
                status: 'posted',
                limit: 100,
              },
            })
          ).data;
          const usual = hist.filter((x) => x.type === 'expense').map((x) => x.amount);
          const t = group.items.find((x) => unusualSpend(x.amount, usual));
          if (t && !quietRef.current()) {
            const what = t.name || t.merchant || 'трата';
            sayRef.current({
              text: `«${what}» заметно больше обычного для этой категории. Это разовая?`,
              kind: 'hello',
              chips: [
                {
                  label: 'Да, разовая',
                  run: () =>
                    void api
                      .patchJson(`/api/v1/money/transactions/${t.id}`, {
                        body: { note: t.note ? `${t.note} · разовая` : 'разовая' },
                      })
                      .then(() => changedRef.current?.('transaction'))
                      .catch(() => undefined),
                },
                { label: 'Нет', run: () => undefined },
              ],
            });
            return;
          }
        } catch {
          // The money module is off or the call failed: nothing to ask.
        }
      }
      // The tips for Tasks and Money carry a count and an amount; each waits for its read.
      const tab = d.pathname.split('/')[1];
      const tabReady =
        tab === 'money'
          ? d.got.report && !hidden
          : tab === 'goals'
            ? d.got.goals
            : tab === 'tasks'
              ? d.context.today != null && !hidden
              : true;
      if (d.pathname !== '/now' && tabReady && once(`tab:${tab}`)) {
        const line = tabTip(d.pathname, {
          inbox: d.context.today?.tasks?.inbox_count,
          spent: d.spent,
          stalled: d.stalled,
        });
        mark(`tab:${tab}`, day);
        if (line) {
          sayRef.current({ text: line, kind: 'hello' });
          return;
        }
      }
      if (
        windDownNow(nightOf(hour, minute), bedtimeOf(arr(d.sleeps), d.context.tz) ?? -9999) &&
        once('windDown')
      ) {
        mark('windDown', day);
        e?.feel('yawn', 3);
        sayRef.current({
          text: 'Через полчаса твое обычное время сна. Давай потихоньку сворачиваться.',
          kind: 'evening',
        });
      }
    };
    const id = window.setInterval(() => void tick(), TICK_MS);
    return () => clearInterval(id);
  }, [on, engine]);
}
