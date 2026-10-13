/** Pure rules behind the calm kit, kept apart so they are testable without a DOM. */

export interface TabItem {
  to: string;
  label: string;
  count?: number;
  end?: boolean;
}

/** Up to @p max tabs stay; more than that keeps max − 1 and folds the rest into "More". */
export function splitTabs<T>(items: T[], max: number): { shown: T[]; more: T[] } {
  if (items.length <= max) return { shown: items, more: [] };
  return { shown: items.slice(0, max - 1), more: items.slice(max - 1) };
}

const WIDTHS: Record<string, number> = {
  'max-w-sm': 384,
  'max-w-md': 448,
  'max-w-lg': 512,
  'max-w-xl': 576,
  'max-w-2xl': 672,
  'max-w-3xl': 768,
};

/** The desktop width of a dialog from the old Modal's max-width class (default max-w-2xl). */
export function dialogWidth(className: string | undefined): number {
  const hit = (className ?? '').split(/\s+/).find((c) => c in WIDTHS);
  return WIDTHS[hit ?? 'max-w-2xl'];
}

/** "−5,000 KZT", "+460,000 KZT", "12.50": a true minus sign, grouped digits, the code after. */
export function formatAmount(
  value: number,
  { currency, signed, digits = 2 }: { currency?: string; signed?: boolean; digits?: number } = {},
): string {
  const body = new Intl.NumberFormat('en-US', {
    minimumFractionDigits: digits,
    maximumFractionDigits: digits,
  }).format(Math.abs(value));
  const sign = value < 0 ? '−' : signed && value > 0 ? '+' : '';
  return `${sign}${body}${currency ? ` ${currency}` : ''}`;
}
