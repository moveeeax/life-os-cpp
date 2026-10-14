// The one number formatter for amounts. Amounts of different currencies are
// never added here or anywhere else; this only prints one number.

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
