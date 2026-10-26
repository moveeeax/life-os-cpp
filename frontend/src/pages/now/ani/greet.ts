import type { NowWeather, Today } from '@/lib/now/types';

/*
 * Her first hello of the day on Now: the hello of the hour and one thing
 * from the day worth saying, in this order: late tasks, rain or snow, the
 * night's sleep in the morning. With nothing to say, or after the first
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
  const n = news(today, weather, hour, hidden);
  return n ? { text: `${OPEN[hour]} ${n}` } : { text: HELLO[hour], recorded: hour };
}
