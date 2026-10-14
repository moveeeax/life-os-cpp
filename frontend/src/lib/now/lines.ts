// The quiet lines under the focus: at most three, each only when something
// is off, in the order late tasks, a goal behind, the number of the day.
import type { Today } from './types';

export interface LinePart {
  text: string;
  /** A number Hide numbers blurs. */
  num?: true;
  /** A title the user wrote: text, never blurred, whatever digits it holds. */
  title?: true;
}

export interface QuietLine {
  key: 'late' | 'goal' | 'protein' | 'money';
  text: string;
  parts: LinePart[];
  to: string;
}

/** The month is "above usual" from this share of the median of the three before. */
export const MONEY_ABOVE_USUAL = 1.15;
/** The protein line waits for the evening, when the day's food is mostly in. */
export const PROTEIN_AFTER_HOUR = 18;
/** And speaks only when this much is still to eat. */
export const PROTEIN_MIN_LEFT = 20;

const amount = new Intl.NumberFormat('en-US', { maximumFractionDigits: 0 });

function line(key: QuietLine['key'], to: string, parts: LinePart[]): QuietLine {
  return { key, to, parts, text: parts.map((p) => p.text).join('') };
}

function numberOfTheDay(today: Today, hour: number): QuietLine | null {
  const food = today.food;
  if (food?.target && hour >= PROTEIN_AFTER_HOUR) {
    const left = Math.round(food.target.protein_g - food.eaten.protein_g);
    if (left >= PROTEIN_MIN_LEFT) {
      return line('protein', '/food', [
        { text: String(left), num: true },
        { text: ' g protein left' },
      ]);
    }
  }
  for (const m of today.money?.currencies ?? []) {
    if (
      m.projection != null &&
      m.median3 != null &&
      m.median3 > 0 &&
      m.projection >= m.median3 * MONEY_ABOVE_USUAL
    ) {
      return line('money', '/money/reports', [
        { text: 'On pace for ' },
        { text: amount.format(m.projection), num: true },
        { text: ` ${m.currency} this month, above usual` },
      ]);
    }
  }
  return null;
}

export function quietLines(today: Today, hour: number): QuietLine[] {
  const out: QuietLine[] = [];
  const tasks = today.tasks;
  if (tasks && tasks.late_count > 0) {
    const title = tasks.first_late ? ` · ${tasks.first_late.title}` : '';
    out.push(
      line('late', '/tasks', [
        { text: String(tasks.late_count), num: true },
        { text: ` late${title}` },
      ]),
    );
  }
  const behind = today.goals?.find((g) => g.pace === 'behind');
  if (behind) {
    out.push(line('goal', '/goals', [{ text: `${behind.title} is behind` }]));
  }
  const n = numberOfTheDay(today, hour);
  if (n) out.push(n);
  return out.slice(0, 3);
}
