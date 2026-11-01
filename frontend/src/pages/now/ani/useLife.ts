import { useQuery } from '@tanstack/react-query';
import { useEffect, useRef } from 'react';

import { useCrossings } from '@/hooks/useTravel';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import { clockIn } from '@/lib/dates';

import type { Ani } from './engine';
import type { AniContext } from './AniCorner';
import {
  achievementsOf,
  addMoment,
  anniversaryOf,
  dealLine,
  energyLine,
  energyOf,
  MORNING_FROM,
  meetingAsk,
  questDone,
  questOf,
  type DealView,
  type Quest,
  type QuestInput,
} from './life';
import type { Answer } from './touch';
import { useWarm } from './warm';

/** How often the corner looks at the day for a line of hers. */
const TICK_MS = 60_000;
/** Seconds after she is on screen before each kind of line may come (the hello goes first). */
const AFTER_S = { energy: 40, quest: 90, anniversary: 150 } as const;
/** The evening diary is offered from this local time until 23:00. */
const DIARY_FROM = { h: 21, m: 30 };

export const DIARY_SEED = 'Давай запишем мой день в дневник.';

const KEY = 'now:ani:life';

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
    // Remembering it is a convenience: at worst she says it once more.
  }
}

/**
 * Her own lines through the day, played by the corner: the energy of the
 * night, the quest of the day and its success, an anniversary, a question
 * after a meeting, a word about the week's promises, an achievement, and the
 * evening diary. One line per tick at most, each once (localStorage), never
 * over another line, the chat, a call or a focus session.
 */
export function useLife({
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
  /** Another line, the chat, a call or a focus is on: she keeps quiet this tick. */
  quiet: () => boolean;
  say: (a: Answer) => void;
  /** The text the chat sends by itself when the person opens it from this line. */
  onSeed: (text: string | null) => void;
}) {
  const { tz, date } = context;
  const warmDeals = useWarm(on, 0);
  const warmFocus = useWarm(on, 1);
  const warmWater = useWarm(on, 2);
  const deals = useQuery({
    queryKey: qk.ani.deals(tz),
    enabled: warmDeals,
    retry: false,
    staleTime: 10 * 60_000,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: DealView[] }>('/api/v1/ani/deals', { query: { tz }, signal }))
        .data,
  });
  const focus = useQuery({
    queryKey: ['now', 'focus-stats', tz],
    enabled: warmFocus,
    retry: false,
    staleTime: 10 * 60_000,
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: { week_minutes: number; longest_minutes: number } }>(
          '/api/v1/now/focus-stats',
          { query: { tz }, signal },
        )
      ).data,
  });
  const water = useQuery({
    queryKey: ['food', 'water', date],
    enabled: warmWater,
    retry: false,
    staleTime: 60_000,
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: { ml: number } }>('/api/v1/food/water', {
          query: { date },
          signal,
        })
      ).data,
  });
  const crossings = useCrossings();

  const live = useRef({
    context,
    deals: deals.data,
    focus: focus.data,
    water: water.data,
    crossings: crossings.data,
  });
  live.current = {
    context,
    deals: deals.data,
    focus: focus.data,
    water: water.data,
    crossings: crossings.data,
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
    const tick = () => {
      if (quietRef.current()) return;
      const { context: c, deals: d, focus: f, water: w, crossings: x } = live.current;
      const day = c.date;
      const seen = read();
      const once = (k: string) => seen[k] !== day;
      const age = (Date.now() - since) / 1000;
      const { hour, minute, weekday } = clockIn(c.tz);
      const e = engine.current;
      const input: QuestInput = {
        overdue: c.today?.tasks?.late_count ?? 0,
        waterMl: w?.ml ?? null,
        focusMinutes: (c.day?.focus ?? []).reduce((n, s) => n + (s.minutes ?? 0), 0),
        hour,
      };

      // The energy of the night, in the morning.
      const energy = energyOf(c.today?.health?.sleep_minutes);
      // From MORNING_FROM: right after midnight the newest night is the one before.
      if (
        hour >= MORNING_FROM &&
        hour < 12 &&
        age >= AFTER_S.energy &&
        once('energy') &&
        energyLine(energy)
      ) {
        mark('energy', day);
        if (energy === 'low') e?.feel('yawn', 3);
        else e?.feel('happy', 3);
        sayRef.current({
          text: energyLine(energy)!,
          mood: energy === 'low' ? -1 : 1,
          kind: 'brief',
        });
        return;
      }

      // The quest of the day: given once, praised once when it is done.
      // A low mood checked in today (useLife2 marks it): no quest that day, she does not push.
      const lowMood = (() => {
        try {
          return (
            (JSON.parse(localStorage.getItem('now:ani:life2') ?? '{}') as Record<string, string>)
              .mood === day
          );
        } catch {
          return false;
        }
      })();
      if (!lowMood && age >= AFTER_S.quest && hour >= 7 && hour < 20 && once('quest')) {
        const q = questOf(input);
        mark('quest', day);
        mark('questOf', JSON.stringify({ day, q, start: input }));
        sayRef.current({ text: q.text, kind: 'hello' });
        return;
      }
      const given = (() => {
        try {
          return JSON.parse(seen.questOf ?? 'null') as {
            day: string;
            q: Quest;
            start: QuestInput;
          } | null;
        } catch {
          return null;
        }
      })();
      if (
        given &&
        given.day === day &&
        once('questDone') &&
        questDone(given.q, input, given.start)
      ) {
        mark('questDone', day);
        addMoment(day, 'Задание дня выполнено');
        e?.feel('happy', 3);
        e?.act('peace');
        sayRef.current({ text: 'Задание дня выполнено! Умница.', mood: 1, kind: 'hello' });
        return;
      }

      // A crossing of this day in an earlier year.
      const anniversary = anniversaryOf(day, x ?? []);
      if (anniversary && age >= AFTER_S.anniversary && once('anniversary')) {
        mark('anniversary', day);
        addMoment(day, anniversary);
        e?.feel('happy', 3);
        sayRef.current({ text: anniversary, kind: 'hello' });
        return;
      }

      // A meeting just over: anything to do after it?
      const asked = new Set((seen.meetings ?? '').split(',').filter(Boolean));
      const meeting = meetingAsk(c.day?.events ?? [], Date.now(), asked);
      if (meeting) {
        mark('meetings', [...asked, meeting.id].slice(-20).join(','));
        seedRef.current(`После встречи «${meeting.title}» есть задачи: `);
        sayRef.current({
          text: `Встреча «${meeting.title}» закончилась. Есть задачи? Нажми и расскажи.`,
          chat: true,
          kind: 'calendar',
        });
        return;
      }

      // The week's promises: a nudge on Wednesday, the result on Sunday evening.
      const dl = dealLine(d ?? [], weekday, hour);
      if (dl && once(`deal${weekday}`)) {
        mark(`deal${weekday}`, day);
        const kept = weekday === 0 && (d ?? [])[0] && d![0].done >= d![0].target;
        if (kept) {
          addMoment(day, `Уговор выполнен: ${d![0].text}`);
          e?.feel('happy', 3);
          e?.act('spin');
        }
        sayRef.current({ text: dl, mood: kept ? 1 : 0, kind: 'hello' });
        return;
      }

      // An achievement of the week, celebrated once.
      const got = (seen.achievements ?? '').split(',').filter(Boolean);
      const fresh = achievementsOf(f ?? null, d ?? []).find((a) => !got.includes(a.id));
      if (fresh) {
        mark('achievements', [...got, fresh.id].slice(-50).join(','));
        addMoment(day, fresh.title);
        e?.feel('happy', 4);
        e?.act('spin');
        sayRef.current({ text: `Новое достижение: ${fresh.title}!`, mood: 1, kind: 'hello' });
        return;
      }

      // The evening diary.
      const late = hour > DIARY_FROM.h || (hour === DIARY_FROM.h && minute >= DIARY_FROM.m);
      if (late && hour < 23 && once('diary')) {
        mark('diary', day);
        seedRef.current(DIARY_SEED);
        sayRef.current({
          text: 'Запишем день в дневник? Нажми на меня, я задам два вопроса.',
          chat: true,
          kind: 'evening',
        });
      }
    };
    const id = window.setInterval(tick, TICK_MS);
    return () => clearInterval(id);
  }, [on, engine]);
}
