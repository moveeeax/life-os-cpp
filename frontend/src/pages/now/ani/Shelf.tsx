import { useQuery } from '@tanstack/react-query';
import { useNavigate } from 'react-router';

import { useCrossings } from '@/hooks/useTravel';
import { api } from '@/lib/api/client';

import { booksOf, coffeeOf, cupsOf, flagOf, plantOf, type Plant } from './shelfItems';
import { useWarm } from './warm';

const DAY_MS = 86_400_000;
const shift = (date: string, by: number) =>
  new Date(Date.parse(`${date}T00:00:00Z`) + by * DAY_MS).toISOString().slice(0, 10);

function useShelfRead<T>(
  key: string,
  path: string,
  query: Record<string, string>,
  on: boolean,
  step: number,
) {
  const warm = useWarm(on, step);
  return useQuery({
    queryKey: ['ani', 'shelf', key, query],
    enabled: warm,
    retry: false,
    staleTime: 30 * 60_000,
    queryFn: async ({ signal }) => (await api.getJson<{ data: T }>(path, { query, signal })).data,
  });
}

const PLANT: Record<Plant, { leaves: number; flower: boolean; droop: boolean }> = {
  sprout: { leaves: 2, flower: false, droop: false },
  bush: { leaves: 5, flower: false, droop: false },
  bloom: { leaves: 5, flower: true, droop: false },
  wilted: { leaves: 3, flower: false, droop: true },
};

function PlantIcon({ p }: { p: Plant }) {
  const s = PLANT[p];
  return (
    <svg viewBox="0 0 24 28" width="22" height="26" aria-hidden="true">
      <path d="M6 20h12l-1.5 7h-9z" fill="#c98a6a" />
      <path d="M12 20V9" stroke="#7fd1a0" strokeWidth="1.4" fill="none" />
      {Array.from({ length: s.leaves }, (_, i) => {
        const y = 18 - i * 2.2;
        const side = i % 2 === 0 ? -1 : 1;
        const tip = s.droop ? y + 3 : y - 2;
        return (
          <path
            key={i}
            d={`M12 ${y} q${side * 4} ${s.droop ? 0 : -1} ${side * 6} ${tip - y}`}
            stroke={s.droop ? '#a8a37a' : '#7fd1a0'}
            strokeWidth="2.2"
            strokeLinecap="round"
            fill="none"
          />
        );
      })}
      {s.flower && <circle cx="12" cy="8" r="2.6" fill="#f0a993" />}
    </svg>
  );
}

const Books = ({ n }: { n: number }) => (
  <svg viewBox="0 0 24 24" width="22" height="22" aria-hidden="true">
    {Array.from({ length: n }, (_, i) => (
      <rect
        key={i}
        x={3 + (i % 2)}
        y={20 - i * 3.6}
        width="17"
        height="3"
        rx="0.8"
        fill={['#a5b4fc', '#f0a993', '#6ee7b7', '#fcd34d', '#c4b5fd'][i]}
      />
    ))}
  </svg>
);

const Cup = () => (
  <svg viewBox="0 0 24 24" width="20" height="20" aria-hidden="true">
    <path d="M7 4h10v5a5 5 0 0 1-10 0z" fill="#fcd34d" />
    <path
      d="M7 6H4a3 3 0 0 0 3 4M17 6h3a3 3 0 0 1-3 4"
      stroke="#fcd34d"
      strokeWidth="1.4"
      fill="none"
    />
    <path d="M10.5 14h3v3h-3zM8 18h8v2H8z" fill="#e5b93a" />
  </svg>
);

const Coffee = ({ full }: { full: boolean }) => (
  <svg viewBox="0 0 24 24" width="20" height="20" aria-hidden="true">
    <path d="M5 9h11v6a5 5 0 0 1-5 5h-1a5 5 0 0 1-5-5z" fill="#e8e2f0" />
    <path d="M16 11h1.5a2.5 2.5 0 0 1 0 5H16" stroke="#e8e2f0" strokeWidth="1.4" fill="none" />
    {full && <path d="M6 10h9v1.6H6z" fill="#7a4a2c" />}
    {full && (
      <path
        d="M9 3c0 2 2 2 2 4M12 3c0 2 2 2 2 4"
        stroke="rgba(255,255,255,.6)"
        strokeWidth="1"
        fill="none"
      />
    )}
  </svg>
);

/**
 * Her shelf on the ledge: what the person earned, drawn flat. Each item opens
 * its section; the reads wait until the page has settled (warm.ts).
 */
export function Shelf({
  on,
  date,
  sleepMinutes,
  hour,
}: {
  on: boolean;
  date: string;
  sleepMinutes?: number | null;
  hour: number;
}) {
  const navigate = useNavigate();
  const habits = useShelfRead<
    { schedule: string; streak: number; best: number; days: { date: string; done: boolean }[] }[]
  >('habits', '/api/v1/habits/days', { from: shift(date, -3), to: date }, on, 20);
  const courses = useShelfRead<unknown[]>(
    'courses',
    '/api/v1/tasks/projects',
    { kind: 'course', status: 'done' },
    on,
    21,
  );
  const deals = useShelfRead<{ done: number; target: number }[]>(
    'deals',
    '/api/v1/ani/deals',
    {},
    on,
    22,
  );
  const crossings = useCrossings();

  const hs = Array.isArray(habits.data) ? habits.data : [];
  const longest = hs.reduce((n, h) => Math.max(n, h.streak ?? 0), 0);
  const yesterday = shift(date, -1);
  const broke = hs.some(
    (h) =>
      h.schedule === 'daily' &&
      h.days.find((d) => d.date === yesterday)?.done === false &&
      h.days.find((d) => d.date === shift(date, -2))?.done === true &&
      (h.best ?? 0) >= 3,
  );
  const plant = plantOf(longest, broke);
  const books = booksOf(Array.isArray(courses.data) ? courses.data.length : 0);
  const cups = cupsOf(Array.isArray(deals.data) ? deals.data : []);
  const coffee = coffeeOf(sleepMinutes, hour);
  const last = [...(crossings.data ?? [])].sort((a, b) => b.date.localeCompare(a.date))[0];
  const flag = flagOf(last?.to_country);
  if (!plant && !books && !cups && !coffee && !flag) return null;

  return (
    <div className="ani-shelf" role="group" aria-label="Her shelf">
      {plant && (
        <button
          type="button"
          title={`Longest habit streak: ${longest} days`}
          onClick={() => void navigate('/habits')}
        >
          <PlantIcon p={plant} />
        </button>
      )}
      {books > 0 && (
        <button
          type="button"
          title={`Courses finished: ${books}`}
          onClick={() => void navigate('/learning')}
        >
          <Books n={books} />
        </button>
      )}
      {Array.from({ length: cups }, (_, i) => (
        <button
          key={i}
          type="button"
          title="A promise kept this week"
          onClick={() => void navigate('/account')}
        >
          <Cup />
        </button>
      ))}
      {coffee && (
        <button
          type="button"
          title={coffee === 'full' ? 'Slept well' : 'A short night'}
          onClick={() => void navigate('/health')}
        >
          <Coffee full={coffee === 'full'} />
        </button>
      )}
      {flag && last && (
        <button
          type="button"
          className="ani-shelf-flag"
          title={`Last crossing: ${last.to_country}, ${last.date}`}
          onClick={() => void navigate('/travel')}
        >
          {flag}
        </button>
      )}
    </div>
  );
}
