/** The POST body of the new-goal form, as the form built it inline (pinned by bodies.test.ts). */
import type { Area } from '@/lib/tasks';

import type { GoalInput, GoalKind } from './index';

const num = (s: string) => Number(s.replace(',', '.'));

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
