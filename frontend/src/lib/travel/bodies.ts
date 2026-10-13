/** The bodies of the travel forms (pinned by bodies.test.ts). */
import type {
  TravelCrossingInput,
  TravelRuleInput,
  TravelTripInput,
  TravelTripItemInput,
} from './types';

const codes = (text: string) =>
  text
    .split(/[\s,]+/)
    .map((c) => c.trim().toUpperCase())
    .filter(Boolean);

export interface CrossingForm {
  date: string;
  from: string;
  to: string;
  time: string;
  place: string;
  note: string;
}

export function crossingBody(f: CrossingForm): TravelCrossingInput {
  return {
    date: f.date,
    from: f.from.trim().toUpperCase(),
    to: f.to.trim().toUpperCase(),
    time: f.time.trim() || null,
    place: f.place.trim(),
    note: f.note.trim(),
  };
}

export interface RuleForm {
  name: string;
  countries: string;
  window_kind: TravelRuleInput['window_kind'];
  window_days: string;
  threshold_days: string;
  threshold_kind: TravelRuleInput['threshold_kind'];
  count_crossing_days: boolean;
  source: string;
}

export function ruleBody(f: RuleForm): TravelRuleInput {
  return {
    name: f.name.trim(),
    countries: codes(f.countries),
    window_kind: f.window_kind,
    window_days: f.window_kind === 'rolling_days' ? Number(f.window_days) : null,
    threshold_days: Number(f.threshold_days),
    threshold_kind: f.threshold_kind,
    count_crossing_days: f.count_crossing_days,
    source: f.source.trim(),
  };
}

export interface TripForm {
  name: string;
  start_date: string;
  end_date: string;
  countries: string;
  note: string;
}

export function tripBody(f: TripForm): TravelTripInput {
  return {
    name: f.name.trim(),
    start_date: f.start_date,
    end_date: f.end_date,
    countries: codes(f.countries),
    note: f.note.trim(),
  };
}

export interface ItemForm {
  kind: TravelTripItemInput['kind'];
  date: string;
  time: string;
  title: string;
  reference: string;
  link: string;
  note: string;
}

export function itemBody(f: ItemForm): TravelTripItemInput {
  return {
    kind: f.kind,
    date: f.date,
    time: f.time.trim() || null,
    title: f.title.trim(),
    reference: f.reference.trim(),
    link: f.link.trim(),
    note: f.note.trim(),
  };
}
