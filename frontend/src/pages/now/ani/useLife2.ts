import { useQuery } from '@tanstack/react-query';
import { useEffect, useRef } from 'react';
import { useLocation } from 'react-router';

import { useGoals } from '@/hooks/useGoals';
import { useMoneySettings, useReport } from '@/hooks/useMoney';
import { useCrossings } from '@/hooks/useTravel';
import { api } from '@/lib/api/client';
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
  unusualSpend,
  windDownNow,
  wishDue,
  type Meal,
} from './life2';
import { arrival } from '../modes/modes';
import { addMoment } from './life';
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

function clockOf(tz: string) {
  const p = new Intl.DateTimeFormat('en-GB', {
    timeZone: tz,
    hour: '2-digit',
    minute: '2-digit',
    hourCycle: 'h23',
  }).formatToParts(new Date());
  return {
    hour: Number(p.find((x) => x.type === 'hour')?.value ?? 0) % 24,
    minute: Number(p.find((x) => x.type === 'minute')?.value ?? 0),
  };
}

/** One GET of the site for her lines: quiet when the module is off (404) or the call fails. */
function useRead<T>(
  key: unknown[],
  path: string,
  query: Record<string, string | number>,
  on: boolean,
  stale = 10 * 60_000,
) {
  return useQuery({
    queryKey: ['ani', 'life', ...key],
    enabled: on,
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
}: {
  on: boolean;
  context: AniContext;
  engine: React.RefObject<Ani | null>;
  quiet: () => boolean;
  say: (a: Answer) => void;
  onSeed: (text: string | null) => void;
}) {
  const { tz, date } = context;
  const { pathname } = useLocation();
  const facts = useRead<{ id: string; text: string }[]>(['facts'], '/api/v1/ani/facts', {}, on);
  const mood = useRead<{ date: string; score: number }[]>(
    ['mood', date],
    '/api/v1/habits/mood',
    { from: date, to: date },
    on,
  );
  const sleeps = useRead<Sleep[]>(
    ['sleep', date],
    '/api/v1/fitness/sleep',
    { from: shift(date, -30), to: date },
    on,
  );
  const habits = useRead<
    { id: string; name: string; schedule: string; days: { date: string; done: boolean }[] }[]
  >(['habit-days', date], '/api/v1/habits/days', { from: shift(date, -5), to: date, tz }, on);
  const steps = useRead<{ date: string; value: number | null }[]>(
    ['steps', date],
    '/api/v1/metrics/series',
    { source: 'fitness:steps', from: shift(date, -30), to: date, tz },
    on,
  );
  const food = useRead<{
    totals: { kcal: number; protein_g: number };
    targets: { kcal: number; protein_g: number } | null;
  }>(['food-day', date], '/api/v1/food/day', { date, tz }, on, 60_000);
  const recent = useRead<Item[]>(['food-recent'], '/api/v1/food/recent', {}, on);
  const goalsProfile = useRead<{ profile: { birth_date: string | null } }>(
    ['food-goals'],
    '/api/v1/food/goals',
    {},
    on,
  );
  const waiting = useRead<{ title: string }[]>(['tg-waiting'], '/api/v1/telegram/waiting', {}, on);
  const spends = useRead<Tx[]>(
    ['tx-today', date],
    '/api/v1/money/transactions',
    { from: date, to: date, status: 'posted', limit: 20 },
    on,
    5 * 60_000,
  );
  const crossings = useCrossings();
  const view = useMoneySettings().data?.view_currency ?? 'KZT';
  const arrived = arrival(crossings.data, date);
  const local = arrived ? CURRENCY_OF[arrived.to_country] : undefined;
  const rate = useRead<{ amount: number }>(
    ['rate', local ?? '', view, date],
    '/api/v1/money/rates/convert',
    { amount: 1, from: local ?? '', to: view, date },
    on && !!local && local !== view,
    60 * 60_000,
  );
  const report = useReport('month', date);
  const goals = useGoals('active', date);

  const live = useRef({
    context,
    pathname,
    facts: facts.data,
    mood: mood.data,
    sleeps: sleeps.data,
    habits: habits.data,
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
    stalled: (goals.data ?? []).find((g) => g.progress?.pace === 'behind')?.title ?? null,
  });
  live.current = {
    context,
    pathname,
    facts: facts.data,
    mood: mood.data,
    sleeps: sleeps.data,
    habits: habits.data,
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
    stalled: (goals.data ?? []).find((g) => g.progress?.pace === 'behind')?.title ?? null,
  };
  const sayRef = useRef(say);
  sayRef.current = say;
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
      const { hour, minute } = clockOf(d.context.tz);
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
        const line = tripHello(d.arrived.to_country, d.local, d.view, d.rate);
        if (line) {
          mark(`trip:${d.arrived.to_country}`, day);
          e?.act('wave');
          sayRef.current({ text: line, kind: 'hello' });
          return;
        }
      }
      if (hour < 12 && age >= 60 && once('bedtimeMorning')) {
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
      if (hour < 12 && age >= 120 && once('records')) {
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
      if (hour < 12 && age >= 180 && once('streak')) {
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
      if (hour >= 8 && hour < 12 && age >= 240 && once('guess')) {
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
      if (hour === 12 && once('tgNoon')) {
        mark('tgNoon', day);
        const w = arr(d.waiting);
        if (w.length > 0) {
          sayRef.current({
            text: `Ждут ответа ${w.length}: главный ${w[0].title}.`,
            kind: 'telegram',
          });
          return;
        }
      }
      for (const [meal, at] of Object.entries(MEAL_HOURS) as [Meal, number][]) {
        if (hour !== at || !once(`meal:${meal}`) || !d.food?.targets) continue;
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
                .then(() =>
                  sayRef.current({
                    text: `Записала: ${o.item.name}.`,
                    silent: true,
                    kind: 'hello',
                  }),
                )
                .catch(() => undefined),
          })),
        });
        return;
      }
      for (const t of arr(d.spends)) {
        if (
          t.type !== 'expense' ||
          !t.category_id ||
          !once(`spend:${t.id}`) ||
          (t.note ?? '').includes('разовая')
        )
          continue;
        mark(`spend:${t.id}`, '9999');
        try {
          const hist = (
            await api.getJson<{ data: Tx[] }>('/api/v1/money/transactions', {
              query: {
                category: t.category_id,
                currency: t.currency,
                from: shift(day, -90),
                to: shift(day, -1),
                status: 'posted',
                limit: 100,
              },
            })
          ).data;
          const odd = unusualSpend(
            t.amount,
            hist.filter((x) => x.type === 'expense').map((x) => x.amount),
          );
          if (!odd || quietRef.current()) continue;
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
                    .catch(() => undefined),
              },
              { label: 'Нет', run: () => undefined },
            ],
          });
          return;
        } catch {
          // The money module is off or the call failed: nothing to ask.
        }
      }
      if (d.pathname !== '/now' && once(`tab:${d.pathname.split('/')[1]}`)) {
        const line = tabTip(d.pathname, {
          inbox: d.context.today?.tasks?.inbox_count,
          spent: d.spent,
          stalled: d.stalled,
        });
        mark(`tab:${d.pathname.split('/')[1]}`, day);
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
