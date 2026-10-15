// The one number formatter and the one parser for amounts. Amounts of
// different currencies are never added here or anywhere else; this only
// prints one number or reads one.

// ── reading a typed amount ─────────────────────────────────────────────────

export interface ParseAmountOptions {
  /** Accept a leading minus (an fx adjustment, an opening balance, a goal value). */
  signed?: boolean;
}

const AMOUNT = /^(\d+)(?:([.,])(\d+))?$/;

/**
 * The number a typed amount means, or null when it is empty or not clear.
 *
 * Accepted: digits with one optional dot or comma and more digits after it.
 * A comma is a decimal separator only when one or two digits follow it:
 * "12,5" and "12,50" are decimals, "1,234" is refused rather than guessed
 * (a thousands group or a mistyped decimal). A dot is never ambiguous, so
 * "1.234" is 1.234. Nothing else is a number here: no exponent, no hex, no
 * spaces inside, no second separator, no sign unless `signed`.
 */
export function parseAmount(input: string, opts: ParseAmountOptions = {}): number | null {
  let t = input.trim();
  if (t === '') return null;
  let negative = false;
  if (opts.signed && t.startsWith('-')) {
    negative = true;
    t = t.slice(1);
  }
  const m = AMOUNT.exec(t);
  if (!m) return null;
  const [, whole, sep, frac] = m;
  if (sep === ',' && frac.length > 2) return null;
  const n = Number(frac === undefined ? whole : `${whole}.${frac}`);
  if (!Number.isFinite(n)) return null;
  return negative && n !== 0 ? -n : n;
}

// ── printing an amount ─────────────────────────────────────────────────────

export interface AmountFormat {
  /** Decimals, fixed (default 2). */
  digits?: number;
  /** The currency: its code after the number, or its sign with `symbol`. */
  currency?: string;
  /** Print with the currency's own sign ("₸13,275.61"); an unknown code falls back to "13275.61 XYZ". */
  symbol?: boolean;
  /** A "+" before a positive amount. */
  signed?: boolean;
  /** The sign of zero when `signed` (default none). */
  zeroSign?: '' | '+';
  /** "−" (U+2212) for a negative amount instead of "-" (default true). */
  trueMinus?: boolean;
  /** Thousands separators (default true). */
  grouping?: boolean;
  /** Drop the decimals from this magnitude up. */
  wholeFrom?: number;
  /** Drop the decimals when the amount has no cents. */
  wholeWhenWhole?: boolean;
}

export function formatAmount(value: number, f: AmountFormat = {}): string {
  let digits = f.digits ?? 2;
  if (f.wholeFrom !== undefined && Math.abs(value) >= f.wholeFrom) digits = 0;
  if (f.wholeWhenWhole && Number.isInteger(Math.round(value * 100) / 100)) digits = 0;

  if (f.symbol && f.currency) {
    try {
      return new Intl.NumberFormat('en-US', {
        style: 'currency',
        currency: f.currency,
        minimumFractionDigits: digits,
        maximumFractionDigits: digits,
      }).format(value);
    } catch {
      // An unknown code: the plain number and the code.
      return `${value.toFixed(digits)} ${f.currency}`;
    }
  }

  const body = new Intl.NumberFormat('en-US', {
    minimumFractionDigits: digits,
    maximumFractionDigits: digits,
    useGrouping: f.grouping ?? true,
  }).format(Math.abs(value));
  const minus = f.trueMinus === false ? '-' : '−';
  const sign =
    value < 0 ? minus : !f.signed ? '' : value > 0 ? '+' : value === 0 ? (f.zeroSign ?? '') : '';
  return `${sign}${body}${f.currency ? ` ${f.currency}` : ''}`;
}
