/**
 * Ani's second set of her own lines, as pure decisions (useLife plays them):
 * the question of the day, follow-ups and wishes from her facts, the mood
 * mirror, bedtime, a lighter habit after a broken streak, personal records,
 * what to eat at each meal, an unusual spend, a hello abroad, the steps
 * guess, the birthday and a tip for the open tab.
 */

const DAY_MS = 86_400_000;
const dayIndex = (date: string) => Math.floor(Date.parse(`${date}T00:00:00Z`) / DAY_MS);
const shift = (date: string, by: number) =>
  new Date(Date.parse(`${date}T00:00:00Z`) + by * DAY_MS).toISOString().slice(0, 10);

export const QUESTIONS = [
  'Как ты больше всего любишь отдыхать?',
  'В какое время дня у тебя больше всего сил?',
  'Что тебя быстрее всего успокаивает?',
  'Какая еда поднимает тебе настроение?',
  'Кому из близких давно пора написать?',
  'Какая привычка дается тебе легче всего?',
  'Что ты хочешь успеть до конца года?',
  'Какая музыка помогает тебе работать?',
  'Где ты отдыхал лучше всего?',
  'Что тебе мешает уснуть вовремя?',
  'Чему ты хочешь научиться в этом месяце?',
  'Какой спорт тебе по душе?',
  'Что для тебя хорошее утро?',
  'Какая книга тебе запомнилась больше всего?',
];

/** The question of @p date, a new one each day in turn. */
export function questionOfDay(date: string): string {
  return QUESTIONS[((dayIndex(date) % QUESTIONS.length) + QUESTIONS.length) % QUESTIONS.length];
}

type Fact = { id: string; text: string };

/** A plan she kept with its day ("2026-11-03: врач") whose day was yesterday. */
export function followUpOf(
  facts: readonly Fact[],
  today: string,
): { id: string; what: string } | null {
  const yesterday = shift(today, -1);
  for (const f of facts) {
    const m = /^(\d{4}-\d{2}-\d{2}): (.+)$/.exec(f.text.trim());
    if (m && m[1] === yesterday) return { id: f.id, what: m[2] };
  }
  return null;
}

/** A wish kept a month or more ago ("Хочу: наушники за 200 долларов (2026-10-01)"). */
export function wishDue(
  facts: readonly Fact[],
  today: string,
): { id: string; what: string } | null {
  for (const f of facts) {
    const m = /^Хочу: (.+) \((\d{4}-\d{2}-\d{2})\)$/.exec(f.text.trim());
    if (m && dayIndex(today) - dayIndex(m[2]) >= 30) return { id: f.id, what: m[1] };
  }
  return null;
}

/** A low mood checked in for @p today (1 or 2 of 5). */
export function moodLow(
  checkins: readonly { date: string; score: number }[],
  today: string,
): boolean {
  return checkins.some((c) => c.date === today && c.score <= 2);
}

/** The local minute a sleep started, past midnight counted as the night before (25:30 for 01:30). */
function nightMinute(iso: string, tz: string): number {
  const p = new Intl.DateTimeFormat('en-GB', {
    timeZone: tz,
    hour: '2-digit',
    minute: '2-digit',
    hourCycle: 'h23',
  }).formatToParts(new Date(iso));
  const h = Number(p.find((x) => x.type === 'hour')?.value ?? 0);
  const m = Number(p.find((x) => x.type === 'minute')?.value ?? 0);
  return (h < 12 ? h + 24 : h) * 60 + m;
}

type Sleep = { start_at: string; end_at: string; duration_minutes: number; is_nap: boolean };

/** The usual bedtime (median of the nights given), in night minutes; null with fewer than five nights. */
export function bedtimeOf(sleeps: readonly Sleep[], tz: string): number | null {
  const starts = sleeps
    .filter((s) => !s.is_nap)
    .map((s) => nightMinute(s.start_at, tz))
    .sort((a, b) => a - b);
  if (starts.length < 5) return null;
  return starts[Math.floor(starts.length / 2)];
}

/** Whether @p now (night minutes) is in the half hour before @p bedtime. */
export function windDownNow(now: number, bedtime: number): boolean {
  return now >= bedtime - 30 && now < bedtime;
}

/** The night minute of a local hour and minute. */
export const nightOf = (hour: number, minute: number) =>
  (hour < 12 ? hour + 24 : hour) * 60 + minute;

/** The morning word about last night against the usual bedtime; null within twenty minutes of it. */
export function bedtimeLine(
  last: Sleep | undefined,
  bedtime: number | null,
  tz: string,
): string | null {
  if (!last || bedtime == null || last.is_nap) return null;
  const diff = nightMinute(last.start_at, tz) - bedtime;
  if (Math.abs(diff) < 20) return null;
  const n = Math.abs(diff);
  const words = n >= 60 ? `${Math.round(n / 60)} ч` : `${n} мин`;
  return diff < 0
    ? `Вчера сон начался на ${words} раньше обычного. Так держать.`
    : `Вчера сон начался на ${words} позже обычного. Сегодня давай пораньше.`;
}

type HabitDays = {
  id: string;
  name: string;
  schedule: string;
  days: { date: string; done: boolean }[];
};

/** A daily habit done three days running and missed yesterday: a lighter version is worth offering. */
export function brokenStreakOf(habits: readonly HabitDays[], today: string): string | null {
  const back = (n: number) => shift(today, -n);
  for (const h of habits) {
    if (h.schedule !== 'daily') continue;
    const done = new Map(h.days.map((d) => [d.date, d.done]));
    if (done.get(back(1)) === false && done.get(back(2)) && done.get(back(3)) && done.get(back(4)))
      return h.name;
  }
  return null;
}

/** Last night is the longest of the month (and longer than any other). */
export function sleepRecord(sleeps: readonly Sleep[]): string | null {
  const nights = sleeps.filter((s) => !s.is_nap);
  if (nights.length < 7) return null;
  const last = nights.reduce((a, b) => (a.end_at > b.end_at ? a : b));
  if (nights.some((s) => s !== last && s.duration_minutes >= last.duration_minutes)) return null;
  const h = Math.floor(last.duration_minutes / 60);
  const m = last.duration_minutes % 60;
  return `Рекорд месяца: самый длинный сон, ${h} ч ${m} мин.`;
}

/** Yesterday's steps are the most of the month. */
export function stepsRecord(
  series: readonly { date: string; value: number | null }[],
  today: string,
): string | null {
  const yesterday = shift(today, -1);
  const y = series.find((p) => p.date === yesterday)?.value;
  const others = series.filter((p) => p.date !== yesterday && p.value != null).map((p) => p.value!);
  if (y == null || others.length < 7 || others.some((v) => v >= y)) return null;
  return `Рекорд месяца по шагам: вчера ${new Intl.NumberFormat('ru-RU').format(Math.round(y))}.`;
}

export type Meal = 'breakfast' | 'lunch' | 'dinner' | 'snack';
/** The local hours when she offers a meal. */
export const MEAL_HOURS: Record<Meal, number> = { breakfast: 8, lunch: 13, snack: 16, dinner: 19 };

type Item = {
  id: string;
  name: string;
  kcal: number;
  protein_g: number;
  servings: { grams: number }[];
};

/**
 * Two of the person's recent products for a meal, the richest in protein per
 * kcal whose serving fits what is left of the day's kcal; each with its
 * serving (or 100 g) and what that brings.
 */
export function mealOptions(
  left: { kcal: number; protein_g: number },
  recent: readonly Item[],
): { item: Item; grams: number; kcal: number; protein: number }[] {
  return recent
    .map((item) => {
      const grams = item.servings[0]?.grams ?? 100;
      return {
        item,
        grams,
        kcal: (item.kcal * grams) / 100,
        protein: (item.protein_g * grams) / 100,
      };
    })
    .filter((o) => o.kcal > 0 && o.kcal <= Math.max(left.kcal, 0) && o.protein >= 5)
    .sort((a, b) => b.protein / b.kcal - a.protein / a.kcal)
    .slice(0, 2);
}

/** A spend at least three times the median of its category (five or more rows of history). */
export function unusualSpend(
  amount: number,
  history: readonly number[],
): { median: number } | null {
  const xs = history.map(Math.abs).sort((a, b) => a - b);
  if (xs.length < 5) return null;
  const median = xs[Math.floor(xs.length / 2)];
  return median > 0 && Math.abs(amount) >= 3 * median ? { median } : null;
}

/** A spend of the day as the unusual-spend check reads it. */
export type SpendRow = {
  id: string;
  type: string;
  category_id: string | null;
  currency: string;
  note?: string;
};

/**
 * The next spends to check against their usual: the expenses of the first
 * category and currency not checked yet, together. One history read covers
 * the whole group, and a tick reads at most one, so twenty expenses of a day
 * no longer send twenty reads at once. A spend already marked "разовая" is
 * left out.
 */
export function nextSpendGroup<T extends SpendRow>(
  spends: readonly T[],
  unchecked: (id: string) => boolean,
): { category: string; currency: string; items: T[] } | null {
  const fresh = spends.filter(
    (t) =>
      t.type === 'expense' &&
      !!t.category_id &&
      unchecked(t.id) &&
      !(t.note ?? '').includes('разовая'),
  );
  const head = fresh[0];
  if (!head?.category_id) return null;
  return {
    category: head.category_id,
    currency: head.currency,
    items: fresh.filter((t) => t.category_id === head.category_id && t.currency === head.currency),
  };
}

const HELLO: Record<string, string> = {
  TH: 'Сават-ди',
  UZ: 'Ассалому алейкум',
  KZ: 'Сәлеметсіз бе',
  KG: 'Саламатсызбы',
  GE: 'Гамарджоба',
  AM: 'Барев',
  TR: 'Мерхаба',
  AE: 'Мархаба',
  VN: 'Синь тяо',
  ID: 'Селамат сианг',
  MY: 'Селамат датанг',
  JP: 'Коннитива',
  KR: 'Аннёнхасэё',
  CN: 'Нихао',
  ES: 'Ола',
  IT: 'Чао',
  FR: 'Бонжур',
  DE: 'Гутен таг',
  RS: 'Здраво',
  ME: 'Здраво',
  CY: 'Ясу',
  GR: 'Ясу',
};

/** Her hello in the country's language, and a price in the person's currency at @p rate. */
export function tripHello(
  country: string,
  local: string | undefined,
  home: string,
  rate: number | null,
): string | null {
  const hi = HELLO[country];
  if (!hi) return null;
  if (!local || local === home || rate == null || rate <= 0) return `${hi}! Мы на новом месте.`;
  const amount =
    local === 'THB' || local === 'TRY' || local === 'AED'
      ? 100
      : local === 'UZS' || local === 'VND' || local === 'IDR' || local === 'KRW'
        ? 100_000
        : 10;
  const fmt = new Intl.NumberFormat('ru-RU', { maximumFractionDigits: 0 });
  return `${hi}! Для ориентира: ${fmt.format(amount)} ${local} это примерно ${fmt.format(amount * rate)} ${home}.`;
}

/** Three guesses of yesterday's steps (thousands), the true one among them, in a fixed order per day. */
export function guessOptions(actual: number, date: string): { label: string; right: boolean }[] {
  const k = Math.max(1, Math.round(actual / 1000));
  const near = k >= 4 ? [k - 3, k, k + 3] : [k, k + 3, k + 6];
  const turn = dayIndex(date) % 3;
  const order = [...near.slice(turn), ...near.slice(0, turn)];
  return order.map((n) => ({ label: `${n} тыс`, right: n === k }));
}

/** Whether @p today is the birthday of @p birth (YYYY-MM-DD). */
export function birthdayToday(birth: string | null | undefined, today: string): boolean {
  return !!birth && birth.slice(5) === today.slice(5);
}

/** One word for the open tab (the shell keeps Now under every tab). */
export function tabTip(
  path: string,
  d: {
    inbox?: number;
    spent?: { amount: number; currency: string } | null;
    stalled?: string | null;
  },
): string | null {
  if (path.startsWith('/tasks') && (d.inbox ?? 0) > 0)
    return `Во входящих ${d.inbox}. Разберем? Пять минут, и чисто.`;
  if (path.startsWith('/money') && d.spent)
    return `В этом месяце ушло около ${new Intl.NumberFormat('ru-RU', { maximumFractionDigits: 0 }).format(d.spent.amount)} ${d.spent.currency}.`;
  if (path.startsWith('/goals') && d.stalled)
    return `Цель «${d.stalled}» отстает от плана. Глянем, что мешает?`;
  return null;
}
