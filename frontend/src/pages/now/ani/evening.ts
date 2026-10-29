import type { Today } from '@/lib/now/types';

/*
 * Her evening word with the day's tasks: how many of today's are done, and
 * an offer to move the open ones to tomorrow (the page asks with buttons).
 */

/** The local day after @p date (YYYY-MM-DD), as YYYY-MM-DD. */
export function tomorrowOf(date: string): string {
  const d = new Date(`${date}T12:00:00Z`);
  d.setUTCDate(d.getUTCDate() + 1);
  return d.toISOString().slice(0, 10);
}

/** The line and the open tasks to move; null without tasks today. */
export function evening(
  today: Today | undefined,
  hidden: boolean,
): { text: string; move: string[] } | null {
  const all = today?.tasks?.today ?? [];
  if (all.length === 0) return null;
  const move = all.filter((t) => t.status !== 'done').map((t) => t.id);
  if (move.length === 0) return { text: 'Все задачи на сегодня сделаны.', move };
  if (hidden) return { text: 'Остались задачи на сегодня. Перенести на завтра?', move };
  const done = all.length - move.length;
  return { text: `Сделано ${done} из ${all.length}. Перенести ${move.length} на завтра?`, move };
}

export interface Waiting {
  chat: string;
  title: string;
  since: string;
}

const SINCE_DAY = [
  'воскресенья',
  'понедельника',
  'вторника',
  'среды',
  'четверга',
  'пятницы',
  'субботы',
];

/** Who waits for an answer in Telegram, in words: up to three names and since when; empty for none. */
export function waitingWords(list: Waiting[], now: Date): string {
  const people = list.filter((w) => w.title.trim() !== '').slice(0, 3);
  if (people.length === 0) return '';
  const oldest = new Date(people[0].since);
  const days = Math.floor((now.getTime() - oldest.getTime()) / 86_400_000);
  const when =
    days <= 1 ? 'со вчера' : days < 7 ? `с ${SINCE_DAY[oldest.getDay()]}` : 'больше недели';
  const names = people.map((w) => w.title.trim());
  const who =
    names.length === 1 ? names[0] : `${names.slice(0, -1).join(', ')} и ${names[names.length - 1]}`;
  const more = list.length > people.length ? ` и еще ${list.length - people.length}` : '';
  return `${who}${more} ${names.length === 1 && !more ? 'ждет' : 'ждут'} ответа в Telegram ${when}.`;
}
