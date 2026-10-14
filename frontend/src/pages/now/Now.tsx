import { useEffect, useState } from 'react';
import { Navigate } from 'react-router';

import { useMe } from '@/hooks/useMe';
import { useNowSettings, useSounds, useToday, useWeather } from '@/hooks/useNow';
import { FOCUS_LENGTHS, greeting, localDate, weatherLine } from '@/lib/now';
import { homeOf } from '@/shell/sections';
import { useGlassNav } from '@/shell/useGlassNav';

import { ArrangeSheet } from './ArrangeSheet';
import { Corners } from './corners/Corners';
import { FocusMode } from './focus/FocusMode';
import { useFocusSession } from './focus/useFocusSession';
import { useSoundPlayer } from './focus/useSoundPlayer';
import { FocusLine } from './FocusLine';
import { FocusPicker } from './FocusPicker';
import { NoteLine } from './NoteLine';
import './now.css';

const LENGTH_KEY = 'now:focus:length';

/** "Focus" and its length under the day's focus. */
function FocusStart({ onStart }: { onStart: (minutes: number) => void }) {
  const [minutes, setMinutes] = useState<number>(() => {
    try {
      const v = Number(globalThis.localStorage?.getItem(LENGTH_KEY));
      return (FOCUS_LENGTHS as readonly number[]).includes(v) ? v : 25;
    } catch {
      return 25;
    }
  });
  const pick = (m: number) => {
    setMinutes(m);
    try {
      globalThis.localStorage?.setItem(LENGTH_KEY, String(m));
    } catch {
      // Remembering the length is a convenience.
    }
  };
  return (
    <div className="n-under">
      <button type="button" className="n-start" onClick={() => onStart(minutes)}>
        Focus
      </button>
      <span className="n-lengths" role="group" aria-label="Focus length">
        {FOCUS_LENGTHS.map((m) => (
          <button key={m} type="button" aria-pressed={m === minutes} onClick={() => pick(m)}>
            {m} min
          </button>
        ))}
      </span>
    </div>
  );
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
 * Now: the picture, the clock, a greeting, the day's one focus and up to
 * three quiet lines when something is off. Paints from the last answer of
 * the day at once.
 */
export function NowPage() {
  const nav = useGlassNav();
  if (nav.nowOn === false) return <Navigate to={homeOf(nav.items)} replace />;
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
  const settings = useNowSettings(me !== null);
  const weather = useWeather(Boolean(settings.data?.city));
  const [arranging, setArranging] = useState(false);
  const [picking, setPicking] = useState(false);
  const session = useFocusSession(me?.id ?? null);
  const player = useSoundPlayer(useSounds(me !== null).data ?? []);
  const data = today.data;
  const focus = data?.focus ?? null;
  const clock = new Intl.DateTimeFormat('en-GB', {
    timeZone: tz,
    hour: '2-digit',
    minute: '2-digit',
  }).format(now);

  return (
    <div className="n-now">
      <div className="n-where">
        {tz.replace(/_/g, ' ')}
        {weather.data && (
          <span className="n-weather">
            {' · '}
            {weatherLine(weather.data).place}{' '}
            <span className="num">{weatherLine(weather.data).degrees}</span>{' '}
            {weatherLine(weather.data).words}
          </span>
        )}
        {settings.data && (
          <button type="button" className="n-arrange" onClick={() => setArranging(true)}>
            Arrange
          </button>
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
            <FocusPicker date={date} today={data} onDone={() => setPicking(false)} />
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
        {data && (
          <NoteLine
            date={date}
            today={data.note?.today ?? null}
            yesterday={data.note?.yesterday ?? null}
          />
        )}
        {today.isError && (
          <span className="n-note">
            {data ? 'Offline: showing the last answer' : 'Could not load the day'}
          </span>
        )}
      </div>
      {data && <Corners today={data} hour={hour} date={date} corners={settings.data?.corners} />}
      {arranging && settings.data && (
        <ArrangeSheet settings={settings.data} onClose={() => setArranging(false)} />
      )}
      {session.phase !== 'idle' && (
        <FocusMode
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
