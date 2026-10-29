/**
 * The world for the Trips map, loaded only when the map opens: Natural Earth
 * country shapes from world-atlas (110m) on a Natural Earth projection, and the
 * GeoNames city list (towns of 30,000 and more, plus capitals; CC BY 4.0).
 */
import { geoCentroid, geoNaturalEarth1, geoPath, type GeoProjection } from 'd3-geo';
import { feature } from 'topojson-client';
import type { Feature, FeatureCollection, Geometry } from 'geojson';
import type { GeometryCollection, Topology } from 'topojson-specification';

import { cityIndex, type CityIndex } from '../year';

import { ALPHA2_OF } from './countryIds';

export const MAP_WIDTH = 1000;

export interface World {
  height: number;
  /** One path per country, keyed by its alpha-2 code. */
  countries: { code: string; d: string }[];
  /** A point inside a country, for a trip whose city is not found. */
  centre: Map<string, [number, number]>;
  project: (lon: number, lat: number) => [number, number] | null;
  cities: CityIndex;
}

let loading: Promise<World> | null = null;

/** Loads the shapes and the cities once per page. */
export function loadWorld(): Promise<World> {
  loading ??= build().catch((e: unknown) => {
    loading = null;
    throw e;
  });
  return loading;
}

async function build(): Promise<World> {
  const [atlas, tsv] = await Promise.all([
    import('world-atlas/countries-110m.json').then((m) => m.default as unknown as Topology),
    import('./cities.tsv?raw').then((m) => m.default),
  ]);
  const all = feature(
    atlas,
    atlas.objects.countries as GeometryCollection<{ name: string }>,
  ) as FeatureCollection<Geometry, { name: string }>;
  // Antarctica takes a fifth of the height and no trip goes there.
  const land = all.features.filter((f) => f.id !== '010');
  const projection: GeoProjection = geoNaturalEarth1().fitWidth(MAP_WIDTH, {
    type: 'FeatureCollection',
    features: land,
  });
  const path = geoPath(projection);
  const [[, top], [, bottom]] = path.bounds({ type: 'FeatureCollection', features: land });
  projection.translate([projection.translate()[0], projection.translate()[1] - top]);
  const countries: World['countries'] = [];
  const centre = new Map<string, [number, number]>();
  for (const f of land as Feature<Geometry, { name: string }>[]) {
    const code = codeOf(f);
    const d = path(f);
    if (!code || !d) continue;
    countries.push({ code, d });
    const p = projection(geoCentroid(f));
    if (p) centre.set(code, p);
  }
  const cities = cityIndex(tsv);
  // A country too small for the 110m shapes: its capital.
  for (const [code, c] of cities.capitals) {
    if (!centre.has(code)) {
      const p = projection([c.lon, c.lat]);
      if (p) centre.set(code, p);
    }
  }
  return {
    height: Math.ceil(bottom - top),
    countries,
    centre,
    project: (lon, lat) => projection([lon, lat]),
    cities,
  };
}

function codeOf(f: Feature<Geometry, { name: string }>): string | undefined {
  if (f.id !== undefined) return ALPHA2_OF[String(f.id)];
  if (f.properties.name === 'Kosovo') return 'XK';
  return undefined;
}
