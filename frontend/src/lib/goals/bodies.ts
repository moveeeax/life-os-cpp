/** The bodies of the goal form: POST for a new goal, PATCH for an edit (pinned by bodies.test.ts). */
import { parseAmount } from '@/lib/amounts';
import type { Area } from '@/lib/tasks';

import type { Goal, GoalInput, GoalKind, GoalPatch } from './index';

// A goal value may be negative (a balance to climb out of); the form has
// already refused what is not a number, so NaN here only means a bypassed form.
const num = (s: string) => parseAmount(s, { signed: true }) ?? NaN;

/** What the goal form holds, as text the way it was typed. */
export interface GoalFormValues {
  title: string;
  area: Area;
  kind: GoalKind;
  due: string;
  why: string;
  from: string;
  to: string;
  unit: string;
  count: string;
  /** A number goal: the catalog id it follows, null for check-ins alone. */
  source?: string | null;
}

export function goalBody(i: GoalFormValues & { today: string }): GoalInput {
  const body: GoalInput = {
    title: i.title.trim(),
    area: i.area,
    kind: i.kind,
    due: i.due,
    start_date: i.today,
    why: i.why.trim(),
  };
  if (i.kind === 'number') {
    Object.assign(body, { start_value: num(i.from), target_value: num(i.to), unit: i.unit.trim() });
    if (i.source) body.source = i.source;
  }
  if (i.kind === 'count') body.target_count = num(i.count);
  return body;
}

/** The form of an existing goal, filled from it. */
export function goalForm(g: Goal): GoalFormValues {
  const text = (v: number | null) => (v == null ? '' : String(v));
  return {
    title: g.title,
    area: g.area as Area,
    kind: g.kind,
    due: g.due,
    why: g.why,
    from: text(g.start_value),
    to: text(g.target_value),
    unit: g.unit,
    count: text(g.target_count),
    source: g.source,
  };
}

/** Only the fields the edit changed; an empty object means nothing to send. */
export function goalPatch(g: Goal, f: GoalFormValues): GoalPatch {
  const p: GoalPatch = {};
  if (f.title.trim() !== g.title) p.title = f.title.trim();
  if (f.area !== g.area) p.area = f.area;
  if (f.why.trim() !== g.why) p.why = f.why.trim();
  if (f.due !== g.due) p.due = f.due;
  if (g.kind === 'number') {
    if (f.unit.trim() !== g.unit) p.unit = f.unit.trim();
    if (num(f.from) !== g.start_value) p.start_value = num(f.from);
    if (num(f.to) !== g.target_value) p.target_value = num(f.to);
    const source = f.source ?? null;
    if (source !== g.source) p.source = source;
  }
  if (g.kind === 'count' && num(f.count) !== g.target_count) p.target_count = num(f.count);
  return p;
}
