import { useQuery } from '@tanstack/react-query';
import { useEffect, useRef, useState } from 'react';
import { Navigate } from 'react-router';

import { useHabitsEnabled } from '@/hooks/useHabits';
import { api } from '@/lib/api/client';
import { useMe } from '@/hooks/useMe';
import {
  useNowSettings,
  useSaveDockOrder,
  useSounds,
  useToday,
  useWallpapers,
  useWeather,
} from '@/hooks/useNow';
import {
  ANI_WALLPAPER_EVENT,
  CORNER_KEYS,
  greeting,
  localDate,
  reorderDock,
  weatherGlyph,
  weatherLine,
  withoutChip,
} from '@/lib/now';
import { homeOf } from '@/shell/sections';
import { useGlassNav } from '@/shell/useGlassNav';

import { AniCorner } from './ani/AniCorner';
import { useFocusInbox } from './ani/inbox';
import { useWeeklyReview } from './review/review';
import { WeeklyReview } from './review/WeeklyReview';
import { aniBase, useAni } from './ani/state';
import { Corners } from './corners/Corners';
import { FocusMode } from './focus/FocusMode';
import { useFocusSession } from './focus/useFocusSession';
import { useSoundPlayer } from './focus/useSoundPlayer';
import { FocusLine } from './FocusLine';
import { FocusPicker } from './FocusPicker';
import { FocusStart } from './FocusStart';
import { ModeCards } from './modes/ModeCards';
import { Board } from './widgets/Board';
import { useNowDay } from './ribbon/layout';
import { Ribbon } from './ribbon/Ribbon';
import './now.css';

/** True while the window is at least this wide. */
function useWide(px: number): boolean {
  const query = `(min-width: ${px}px)`;
  const [wide, setWide] = useState(
    () => typeof matchMedia === 'function' && matchMedia(query).matches,
  );
  useEffect(() => {
    if (typeof matchMedia !== 'function') return;
    const m = matchMedia(query);
    const on = () => setWide(m.matches);
    m.addEventListener('change', on);
    return () => m.removeEventListener('change', on);
  }, [query]);
  return wide;
}

/** The current time, updated on the minute. */
function useMinute(): Date {
  const [now, setNow] = useState(() => new Date());
  useEffect(() => {
    let interval: ReturnType<typeof setInterval> | undefined;
    const first = setTimeout(
      () => {
        setNow(new Date());
        interval = setInterval(() => setNow(new Date()), 60_000);
      },
      60_000 - (Date.now() % 60_000),
    );
    return () => {
      clearTimeout(first);
      if (interval) clearInterval(interval);
    };
  }, []);
  return now;
}

/**
 * Now: the picture, the clock, a greeting, the day's one focus, the weather
 * top right, the dock of the day's state at the bottom and the action
 * corners. Paints from the last answer of the day at once.
 */
export function NowPage() {
  const nav = useGlassNav();
  // Now is off: go to the first section that is on, once every probe has
  // answered (a module still asking would count as on and win the redirect).
  if (nav.nowOn === false) return nav.settled ? <Navigate to={homeOf(nav.items)} replace /> : null;
  return <NowScreen />;
}

function NowScreen() {
  const me = useMe().data ?? null;
  const tz = Intl.DateTimeFormat().resolvedOptions().timeZone || 'UTC';
  const now = useMinute();
  const date = localDate(now, tz);
  const hour =
    Number(
      new Intl.DateTimeFormat('en-GB', { timeZone: tz, hour: '2-digit', hour12: false }).format(
        now,
      ),
    ) % 24;
  const today = useToday(me?.id ?? null, date, tz);
  const nowDay = useNowDay(date, tz);
  // The weekly review: by itself on Sunday from 10:00 (once), or from the account menu.
  const review = useWeeklyReview(date);
  const settings = useNowSettings(me?.id ?? null);
  const weather = useWeather(Boolean(settings.data?.city));
  const habitsOn = useHabitsEnabled(Boolean(me));
  const [picking, setPicking] = useState(false);
  const session = useFocusSession(me?.id ?? null);
  // While a session runs Ani keeps quiet; the page still counts what waits for her.
  const aniWaiting = useQuery({
    queryKey: ['ani', 'inbox', 'count'],
    enabled: session.phase === 'running' || session.phase === 'paused',
    refetchInterval: 15_000,
    retry: false,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: { waiting: number } }>('/api/v1/ani/inbox/count', { signal })).data
        .waiting,
  });
  // Her webhook or MCP can stop or replace a running focus session.
  const onAniFocus = (action: 'start' | 'stop', minutes?: number) =>
    action === 'start' && minutes ? session.start(minutes) : session.exit();
  useFocusInbox(session.phase !== 'idle', onAniFocus);
  const player = useSoundPlayer(useSounds(me !== null).data ?? []);
  const data = today.data;
  const focus = data?.focus ?? null;
  const saveOrder = useSaveDockOrder(me?.id ?? null);
  // The dock shows the corners without Habits when that module is off; a
  // drag reorders what is shown and the hidden slot keeps its chip.
  const raw = settings.data?.corners;
  const shown = raw && !habitsOn ? withoutChip(raw, 'habits') : raw;
  const reorder = (from: number, to: number) => {
    if (!raw || !shown) return;
    const next = reorderDock(shown, from, to);
    for (const k of CORNER_KEYS) if (next[k] == null && raw[k]) next[k] = raw[k];
    saveOrder(next);
  };
  // Ani: on a desktop wide enough for her ledge left of the dock, out of the
  // way during a focus session. Her files sit next to the wallpapers.
  const [ani] = useAni();
  const wide = useWide(1024);
  const base = aniBase(useWallpapers('desktop', ani.on && wide).data?.[0]?.url);
  const showAni = ani.on && wide && base !== null && session.phase === 'idle';
  // A session that reached its end (not one left early) sends her back
  // with a word about a break.
  const [afterFocus, setAfterFocus] = useState(false);
  const lastPhase = useRef(session.phase);
  useEffect(() => {
    if (session.phase === 'idle' && lastPhase.current === 'done') setAfterFocus(true);
    if (session.phase === 'running') setAfterFocus(false);
    lastPhase.current = session.phase;
  }, [session.phase]);
  const clock = new Intl.DateTimeFormat('en-GB', {
    timeZone: tz,
    hour: '2-digit',
    minute: '2-digit',
  }).format(now);

  return (
    <div className="n-now" data-ani={showAni ? '' : undefined}>
      <div className="n-where">
        {weather.data ? (
          // The weather takes the place of the zone: the city says where.
          <span className="n-weather">
            <span className="n-deg num">{weatherLine(weather.data).degrees}</span>
            <span className="n-place">
              {weatherLine(weather.data).place}, {weather.data.words}
              {weatherGlyph(weather.data.code, weather.data.is_day) && (
                <>
                  {' '}
                  <span className="n-glyph" aria-hidden="true">
                    {weatherGlyph(weather.data.code, weather.data.is_day)}
                  </span>
                </>
              )}
            </span>
          </span>
        ) : (
          tz.replace(/_/g, ' ')
        )}
      </div>
      <div className="n-center">
        <div className="n-clock" aria-label={`Time ${clock}`}>
          {clock}
        </div>
        <div className="n-hello">{greeting(hour, me?.first_name)}</div>
        <div className="n-focus">
          {focus && !picking ? (
            <>
              <span className="n-label">Today</span>
              <FocusLine focus={focus} onChange={() => setPicking(true)} />
            </>
          ) : data || !today.isPending ? (
            <FocusPicker date={date} onDone={() => setPicking(false)} />
          ) : null}
          {picking && (
            <button type="button" className="n-change" onClick={() => setPicking(false)}>
              Keep the current focus
            </button>
          )}
        </div>
        <FocusStart
          onStart={(m) => {
            session.start(m, focus?.date ?? null);
            player.play();
          }}
        />
        <Ribbon
          date={date}
          tz={tz}
          now={now.getTime()}
          focusFrom={session.startedAt}
          onFocus={(m) => {
            session.start(m, focus?.date ?? null);
            player.play();
          }}
        />
        <ModeCards now={now} date={date} today={data} day={nowDay.data} />
        <Board widgets={settings.data?.widgets ?? []} today={data} date={date} tz={tz} />
        {today.isError && (
          <span className="n-note">
            {data ? 'Offline: showing the last answer' : 'Could not load the day'}
          </span>
        )}
      </div>
      {data && (
        <Corners
          today={data}
          hour={hour}
          date={date}
          corners={shown}
          links={settings.data?.links ?? []}
          onReorder={raw ? reorder : undefined}
        />
      )}
      {review.open && (
        <WeeklyReview today={today.data} date={date} tz={tz} onClose={review.close} />
      )}
      {showAni && base && (
        <AniCorner
          base={base}
          context={{
            name: me?.first_name ?? undefined,
            tz,
            date,
            today: today.data,
            weather: weather.data ?? undefined,
            day: nowDay.data,
          }}
          afterFocus={afterFocus}
          onAfterFocus={() => setAfterFocus(false)}
          onFocus={onAniFocus}
          onTasksMoved={() => void today.refetch()}
          onWallpaper={(which) =>
            window.dispatchEvent(new CustomEvent(ANI_WALLPAPER_EVENT, { detail: { which } }))
          }
          onSound={(which) => {
            // A browser may refuse sound before the first click: then the
            // choice stays and the next click starts it.
            if (which === 'off') player.stop();
            else if (which) {
              player.choose(which);
              if (player.state !== 'playing')
                player.play(player.sounds.find((x) => x.id === which));
            } else player.play();
          }}
        />
      )}
      {session.phase !== 'idle' && (
        <FocusMode
          waiting={aniWaiting.data ?? 0}
          phase={session.phase}
          left={session.left}
          focus={focus && focus.date === session.focusDate ? focus : null}
          player={player}
          onPause={() => {
            session.pause();
            player.stop();
          }}
          onResume={() => {
            session.resume();
            player.play();
          }}
          onExit={() => {
            player.stop(true);
            session.exit();
          }}
          onAgain={() => {
            session.start(session.minutes || 25, focus?.date ?? null);
            player.play();
          }}
        />
      )}
    </div>
  );
}
