/**
 * What Ani does on her own during the day, as pure decisions the corner
 * plays (AniCorner): her energy from the night's sleep, one small quest a
 * day, anniversaries of crossings, a question after a meeting, a word about
 * the week's promises, and the achievements she celebrates. Each line comes
 * at most once (the corner keeps the keys).
 */

export const SLEEP_LOW_MINUTES = 360;
export const SLEEP_GOOD_MINUTES = 450;

export type Energy = 'low' | 'ok' | 'high';

/** Her energy from the minutes the person slept; null without a night. */
export function energyOf(sleepMinutes: number | null | undefined): Energy | null {
  if (sleepMinutes == null || sleepMinutes <= 0) return null;
  if (sleepMinutes < SLEEP_LOW_MINUTES) return 'low';
  return sleepMinutes >= SLEEP_GOOD_MINUTES ? 'high' : 'ok';
}

/** What she says about it in the morning; nothing for an ordinary night. */
export function energyLine(e: Energy | null): string | null {
  if (e === 'low')
    return 'Сна меньше шести часов. Давай тяжелое после обеда, а сейчас что-нибудь простое.';
  if (e === 'high') return 'Хороший сон, отличный день для самой сложной задачи.';
  return null;
}

export interface QuestInput {
  overdue: number;
  /** Water today in ml; null while the food module is off. */
  waterMl: number | null;
  focusMinutes: number;
  hour: number;
}

export type Quest = { id: 'overdue' | 'water' | 'focus'; text: string };

export const QUEST_WATER_ML = 500;
export const QUEST_FOCUS_MINUTES = 20;

/** The one quest of the day: a late task first, water in the morning, otherwise focus. */
export function questOf(i: QuestInput): Quest {
  if (i.overdue > 0)
    return { id: 'overdue', text: 'Задание дня: закрой одну просроченную задачу.' };
  if (i.hour < 12 && i.waterMl != null && i.waterMl < QUEST_WATER_ML)
    return { id: 'water', text: 'Задание дня: два стакана воды до полудня.' };
  return { id: 'focus', text: 'Задание дня: двадцать минут фокуса.' };
}

/** Whether @p q is done now (@p i), against the day as it was when she gave it (@p start). */
export function questDone(q: Quest, i: QuestInput, start: QuestInput): boolean {
  if (q.id === 'overdue') return i.overdue < start.overdue;
  if (q.id === 'water') return (i.waterMl ?? 0) >= QUEST_WATER_ML;
  return i.focusMinutes >= QUEST_FOCUS_MINUTES;
}

const years = (n: number) =>
  n === 1
    ? 'Год'
    : `${n} ${n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14) ? 'года' : 'лет'}`;

/** A crossing of this day in an earlier year, as her line; the most recent one. */
export function anniversaryOf(
  today: string,
  crossings: readonly { date: string; to_country: string }[],
): string | null {
  const md = today.slice(5);
  const year = Number(today.slice(0, 4));
  const hit = [...crossings]
    .filter((c) => c.date.slice(5) === md && Number(c.date.slice(0, 4)) < year)
    .sort((a, b) => b.date.localeCompare(a.date))[0];
  if (!hit) return null;
  let country = hit.to_country;
  try {
    country = new Intl.DisplayNames(['ru'], { type: 'region' }).of(hit.to_country) ?? country;
  } catch {
    // The code stays.
  }
  return `${years(year - Number(hit.date.slice(0, 4)))} назад в этот день был въезд в страну: ${country}.`;
}

const ASK_WITHIN_MS = 15 * 60_000;
const MEETING_MIN_MS = 15 * 60_000;

/** A meeting that ended in the last quarter of an hour and lasted a quarter of an hour or more. */
export function meetingAsk(
  events: readonly { id: string; title: string; start: string; end: string; all_day: boolean }[],
  now: number,
  asked: ReadonlySet<string>,
): { id: string; title: string } | null {
  for (const e of events) {
    if (e.all_day || asked.has(e.id)) continue;
    const start = Date.parse(e.start);
    const end = Date.parse(e.end);
    if (end <= now && now - end <= ASK_WITHIN_MS && end - start >= MEETING_MIN_MS)
      return { id: e.id, title: e.title };
  }
  return null;
}

export interface DealView {
  id: string;
  text: string;
  target: number;
  done: number;
}

/** Wednesday: a nudge for a promise under half way. Sunday from 18:00: how each went. */
export function dealLine(deals: readonly DealView[], weekday: number, hour: number): string | null {
  if (weekday === 3) {
    const behind = deals.find((d) => d.done * 2 < d.target);
    return behind
      ? `Наш уговор «${behind.text}»: пока ${behind.done} из ${behind.target}. Успеваем?`
      : null;
  }
  if (weekday === 0 && hour >= 18 && deals.length > 0) {
    const d = deals[0];
    return d.done >= d.target
      ? `Уговор «${d.text}» выполнен, я тобой горжусь!`
      : `«${d.text}»: ${d.done} из ${d.target}. Ничего, на следующей неделе получится.`;
  }
  return null;
}

export type Achievement = { id: string; title: string };

/** What she celebrates this week: ten hours of focus, a session over 90 minutes, each kept promise. */
export function achievementsOf(
  focus: { week_minutes: number; longest_minutes: number } | null,
  deals: readonly DealView[],
): Achievement[] {
  const out: Achievement[] = [];
  if (focus && focus.week_minutes >= 600)
    out.push({ id: 'focus10h', title: 'Десять часов фокуса за неделю' });
  if (focus && focus.longest_minutes >= 90)
    out.push({ id: 'deep90', title: 'Фокус дольше полутора часов подряд' });
  for (const d of deals)
    if (d.done >= d.target) out.push({ id: `deal:${d.id}`, title: `Уговор выполнен: ${d.text}` });
  return out;
}

const ALBUM_KEY = 'now:ani:album';
export type Moment = { date: string; title: string };

/** The moments of her album in this browser, newest first. */
export function readAlbum(): Moment[] {
  try {
    const v = JSON.parse(localStorage.getItem(ALBUM_KEY) ?? '[]') as Moment[];
    return Array.isArray(v) ? v : [];
  } catch {
    return [];
  }
}

/** Keeps a moment (an achievement, a kept promise, an anniversary, a record) once per title and day. */
export function addMoment(date: string, title: string): void {
  try {
    const all = readAlbum();
    if (all.some((m) => m.date === date && m.title === title)) return;
    localStorage.setItem(ALBUM_KEY, JSON.stringify([{ date, title }, ...all].slice(0, 200)));
  } catch {
    // The album is a convenience of this browser.
  }
}
