import type { NowWeather, Today } from '@/lib/now/types';

/*
 * Her first hello of the day on Now: in the morning a brief of the day
 * (brief); later the hello of the hour and one thing from the day worth
 * saying, in this order: late tasks, rain or snow. With nothing to say, or after the first
 * hello of the day, she uses a recorded hello of the hour.
 */

export type Hour = 'morning' | 'day' | 'evening' | 'night';

/** The recorded hello of an hour (v2/voice/<hour>.mp3). */
export const HELLO: Record<Hour, string> = {
  morning: 'Доброе утро! Я уже тут.',
  day: 'Привет! Как идут дела?',
  evening: 'Добрый вечер. Долгий был день?',
  night: 'Уже поздно. Может, пора спать?',
};

export function hourOf(h: number): Hour {
  return h < 5 ? 'night' : h < 12 ? 'morning' : h < 18 ? 'day' : h < 23 ? 'evening' : 'night';
}

// The opening of a hello with more to say.
const OPEN: Record<Hour, string> = {
  morning: 'Доброе утро!',
  day: 'Привет!',
  evening: 'Добрый вечер!',
  night: 'Не спится?',
};

/** "1 задача", "3 задачи", "5 задач". */
export function plural(n: number, one: string, few: string, many: string): string {
  const d = n % 10;
  const dd = n % 100;
  if (d === 1 && dd !== 11) return one;
  if (d >= 2 && d <= 4 && (dd < 12 || dd > 14)) return few;
  return many;
}

// WMO weather codes (open-meteo.com/en/docs, "WMO Weather interpretation codes").
const RAIN = new Set([51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 80, 81, 82, 95, 96, 99]);
const SNOW = new Set([71, 73, 75, 77, 85, 86]);

/** The one thing from the day she mentions, or null. */
export function news(
  today: Today | undefined,
  weather: NowWeather | undefined,
  hour: Hour,
  hidden: boolean,
): string | null {
  const late = today?.tasks?.late_count ?? 0;
  if (late === 1) return 'Одна задача просрочена, глянешь?';
  if (late > 1 && hidden) return 'Есть просроченные задачи, глянешь?';
  if (late > 1)
    return `${late} ${plural(late, 'задача просрочена', 'задачи просрочены', 'задач просрочено')}, глянешь?`;
  if (weather && RAIN.has(weather.code)) return 'Сегодня дождь, зонт не забудь.';
  if (weather && SNOW.has(weather.code)) return 'Сегодня снег, оденься потеплее.';
  const slept = today?.health?.sleep_minutes;
  if (hour === 'morning' && slept != null && slept > 0 && !hidden) {
    const h = Math.floor(slept / 60);
    const m = slept % 60;
    const time =
      `${h} ${plural(h, 'час', 'часа', 'часов')}` +
      (m ? ` ${m} ${plural(m, 'минута', 'минуты', 'минут')}` : '');
    return slept < 7 * 60 ? `${time} сна. Береги себя сегодня.` : `${time} сна. Бодрячком?`;
  }
  return null;
}

/** The longest morning brief, so the hello with it stays one spoken line. */
const BRIEF_MAX = 185;

const sentence = (s: string) => s.charAt(0).toUpperCase() + s.slice(1) + '.';

/**
 * Her morning brief: up to three facts of the day in this order, today's
 * tasks (and late ones), habits left, the weather, the night's sleep; no
 * personal numbers while they are hidden. Null with nothing to say.
 */
export function brief(
  today: Today | undefined,
  weather: NowWeather | undefined,
  hidden: boolean,
): string | null {
  const facts: string[] = [];
  const open = (today?.tasks?.today ?? []).filter((t) => t.status !== 'done').length;
  const late = today?.tasks?.late_count ?? 0;
  if (open > 0 || late > 0) {
    if (hidden) facts.push('задачи на сегодня ждут');
    else {
      const parts: string[] = [];
      if (open > 0) parts.push(`на сегодня ${open} ${plural(open, 'задача', 'задачи', 'задач')}`);
      if (late === 1) parts.push('одна просрочена');
      else if (late > 1)
        parts.push(`${late} ${plural(late, 'просрочена', 'просрочены', 'просрочено')}`);
      facts.push(parts.join(', '));
    }
  }
  const habits = today?.habits?.left.length ?? 0;
  if (habits > 0)
    facts.push(
      hidden
        ? 'привычки ждут'
        : `${habits} ${plural(habits, 'привычка ждет', 'привычки ждут', 'привычек ждут')}`,
    );
  if (weather) {
    const t = Math.round(weather.temperature);
    const word = RAIN.has(weather.code)
      ? ', дождь, зонт не забудь'
      : SNOW.has(weather.code)
        ? ', снег'
        : '';
    facts.push(`${t > 0 ? '+' : ''}${t}°${word}`);
  }
  const slept = today?.health?.sleep_minutes;
  if (!hidden && slept != null && slept > 0) {
    const h = Math.floor(slept / 60);
    const m = slept % 60;
    facts.push(
      `${h} ${plural(h, 'час', 'часа', 'часов')}` +
        (m ? ` ${m} ${plural(m, 'минута', 'минуты', 'минут')}` : '') +
        ' сна',
    );
  }
  let out = '';
  for (const f of facts.slice(0, 3)) {
    const next = out ? `${out} ${sentence(f)}` : sentence(f);
    if (next.length > BRIEF_MAX) break;
    out = next;
  }
  return out || null;
}

/**
 * Her hello: the text, and the recording to play when it is only the hello
 * of the hour (a hello with the day in it is read by the server).
 */
export function greeting(
  today: Today | undefined,
  weather: NowWeather | undefined,
  h: number,
  hidden: boolean,
): { text: string; recorded?: Hour } {
  const hour = hourOf(h);
  if (hour === 'morning') {
    const b = brief(today, weather, hidden);
    return b ? { text: `${OPEN.morning} ${b}` } : { text: HELLO.morning, recorded: 'morning' };
  }
  const n = news(today, weather, hour, hidden);
  return n ? { text: `${OPEN[hour]} ${n}` } : { text: HELLO[hour], recorded: hour };
}
