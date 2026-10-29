import { useEffect, useMemo, useRef, useState } from 'react';

import { placeDays, placesOf, type Place } from '@/lib/travel/geo/places';
import { MAP_WIDTH, loadWorld, type World } from '@/lib/travel/geo/world';
import { countryName, type TravelTrip } from '@/lib/travel';

const radius = (days: number) => Math.max(3, 2.3 * Math.sqrt(Math.max(days, 1)));
const FONT = 15;

interface Box {
  x0: number;
  y0: number;
  x1: number;
  y1: number;
}
const hits = (a: Box, b: Box) => a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;

/** Right of the point when free, else left, below or above; the first free side wins. */
function placeLabels(places: Place[], text: (p: Place) => string) {
  const taken: Box[] = places.map((p) => {
    const r = radius(Math.max(p.plan, p.done));
    return { x0: p.x - r, y0: p.y - r, x1: p.x + r, y1: p.y + r };
  });
  const out = new Map<string, { x: number; y: number; anchor: 'start' | 'end' | 'middle' }>();
  for (const p of places) {
    const r = radius(Math.max(p.plan, p.done)) + 5;
    const w = text(p).length * FONT * 0.55;
    const sides = [
      {
        x: p.x + r,
        y: p.y + FONT / 3,
        anchor: 'start' as const,
        box: [p.x + r, p.y - FONT / 2, p.x + r + w, p.y + FONT / 2],
      },
      {
        x: p.x - r,
        y: p.y + FONT / 3,
        anchor: 'end' as const,
        box: [p.x - r - w, p.y - FONT / 2, p.x - r, p.y + FONT / 2],
      },
      {
        x: p.x,
        y: p.y + r + FONT,
        anchor: 'middle' as const,
        box: [p.x - w / 2, p.y + r, p.x + w / 2, p.y + r + FONT * 1.2],
      },
      {
        x: p.x,
        y: p.y - r - 4,
        anchor: 'middle' as const,
        box: [p.x - w / 2, p.y - r - FONT * 1.2, p.x + w / 2, p.y - r],
      },
    ];
    const free =
      sides.find((s) => {
        const b = { x0: s.box[0], y0: s.box[1], x1: s.box[2], y1: s.box[3] };
        return b.x0 >= 0 && b.x1 <= MAP_WIDTH && !taken.some((t) => hits(t, b));
      }) ?? sides[0];
    taken.push({ x0: free.box[0], y0: free.box[1], x1: free.box[2], y1: free.box[3] });
    out.set(p.key, { x: free.x, y: free.y, anchor: free.anchor });
  }
  return out;
}

/** The year's places on a world map: dot area by days, the running trip's plan as a ring. */
export default function TripsMap({
  year,
  trips,
  today,
  selected,
  onPick,
}: {
  year: number;
  trips: TravelTrip[];
  today: string;
  selected: string[];
  onPick: (ids: string[]) => void;
}) {
  const [world, setWorld] = useState<World | null>(null);
  const [failed, setFailed] = useState(false);
  const wrap = useRef<HTMLDivElement>(null);
  useEffect(() => {
    let live = true;
    loadWorld().then(
      (w) => live && setWorld(w),
      () => live && setFailed(true),
    );
    return () => {
      live = false;
    };
  }, []);

  const places = useMemo(
    () => (world ? placesOf(world, trips, today, year) : []),
    [world, trips, today, year],
  );
  const countries = useMemo(() => new Set(trips.flatMap((t) => t.countries)), [trips]);
  const labels = useMemo(() => placeLabels(places, (p) => `${p.name} ${placeDays(p)}`), [places]);

  // On a phone the map is wider than the sheet: open it on this year's points.
  useEffect(() => {
    const el = wrap.current;
    if (!el || places.length === 0 || el.scrollWidth <= el.clientWidth + 4) return;
    const xs = places.map((p) => p.x);
    const mid = (Math.min(...xs) + Math.max(...xs)) / 2;
    el.scrollLeft = (mid / MAP_WIDTH) * el.scrollWidth - el.clientWidth / 2;
  }, [places]);

  if (failed) return <p className="k-sub">The map did not load. Try again later.</p>;
  if (!world) return <div className="t-map-ph" aria-hidden="true" />;
  const live = places.some((p) => p.plan > p.done);
  return (
    <>
      <div className="t-map-wrap" ref={wrap}>
        <svg
          viewBox={`0 0 ${MAP_WIDTH} ${world.height}`}
          role="group"
          aria-label={`Places of ${year}, dot area by days`}
        >
          {world.countries.map((c) => (
            <path
              key={c.code}
              d={c.d}
              className={countries.has(c.code) ? 't-land t-been' : 't-land'}
            />
          ))}
          {places.map((p) => {
            const l = labels.get(p.key)!;
            const on = p.trips.some((id) => selected.includes(id));
            return (
              <g
                key={p.key}
                className="t-pt"
                role="button"
                tabIndex={0}
                aria-pressed={on}
                aria-label={`${p.name}, ${countryName(p.country)}, ${placeDays(p)}`}
                onClick={() => onPick(p.trips)}
                onKeyDown={(e) => {
                  if (e.key === 'Enter' || e.key === ' ') {
                    e.preventDefault();
                    onPick(p.trips);
                  }
                }}
              >
                <circle className="t-hit" cx={p.x} cy={p.y} r={Math.max(radius(p.plan), 11)} />
                {p.plan > p.done && (
                  <circle className="t-plan" cx={p.x} cy={p.y} r={radius(p.plan)} />
                )}
                <circle className="t-fill" cx={p.x} cy={p.y} r={radius(p.done)} />
                <text x={l.x} y={l.y} textAnchor={l.anchor} data-num-ok>
                  {p.name} <tspan>{placeDays(p)}</tspan>
                </text>
              </g>
            );
          })}
        </svg>
      </div>
      <p className="t-legend">
        Dot area: days in {year}
        {live ? ' · dashed ring: days still planned' : ''} · cities are read from trip names · map
        Natural Earth, cities GeoNames
      </p>
    </>
  );
}
