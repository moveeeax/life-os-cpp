/** The POST body of the new-goal form, as the form built it inline (pinned by bodies.test.ts). */
import { parseAmount } from '@/lib/amounts';
import type { Area } from '@/lib/tasks';

import type { GoalInput, GoalKind } from './index';

// A goal value may be negative (a balance to climb out of); the form has
// already refused what is not a number, so NaN here only means a bypassed form.
const num = (s: string) => parseAmount(s, { signed: true }) ?? NaN;

export function goalBody(i: {
  title: string;
  area: Area;
  kind: GoalKind;
  due: string;
  today: string;
  why: string;
  from: string;
  to: string;
  unit: string;
  count: string;
}): GoalInput {
  const body: GoalInput = {
    title: i.title.trim(),
    area: i.area,
    kind: i.kind,
    due: i.due,
    start_date: i.today,
    why: i.why.trim(),
  };
  if (i.kind === 'number')
    Object.assign(body, { start_value: num(i.from), target_value: num(i.to), unit: i.unit.trim() });
  if (i.kind === 'count') body.target_count = num(i.count);
  return body;
}
