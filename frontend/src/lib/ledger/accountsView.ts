// The Accounts list by what each account is worth in US dollars: a BTC
// balance of 0.05 is a small number and a large value, so both hiding and
// sorting go by the dollar value from /money/summary, never by the balance.

/** An account of the summary: its dollar value and its currency's rate. */
export interface SummaryRow {
  id: string;
  currency: string;
  /** Units of the account currency per 1 USD. */
  rate: number;
  value: number;
}

export interface Valued<A> {
  account: A;
  /** In USD; null when the currency has no rate on the summary's day. */
  value: number | null;
}

/** The "hide under" choices, in USD; 0 shows every account. */
export const SMALL_LIMITS = [0, 1, 10, 100, 1000] as const;

/**
 * The dollar value of each account: the summary's own when it converted the
 * account, else the balance at the rate of the account's currency (the summary
 * leaves archived accounts out, but their currency usually has a rate there).
 */
export function valueAccounts<A extends { id: string; currency: string; balance: number }>(
  accounts: A[],
  rows: SummaryRow[],
): Valued<A>[] {
  const byId = new Map(rows.map((r) => [r.id, r.value]));
  const rateOf = new Map(rows.filter((r) => r.rate > 0).map((r) => [r.currency, r.rate]));
  return accounts.map((account) => {
    const own = byId.get(account.id);
    if (own !== undefined) return { account, value: own };
    const rate = rateOf.get(account.currency);
    return { account, value: rate ? account.balance / rate : null };
  });
}

/**
 * The accounts worth at least @p limit dollars (by size, so a debt counts),
 * largest first; an account without a value is never hidden and goes last.
 */
export function viewAccounts<A extends { name: string }>(
  list: Valued<A>[],
  limit: number,
): { shown: Valued<A>[]; hidden: number } {
  const shown = list.filter((v) => v.value === null || Math.abs(v.value) >= limit);
  shown.sort((x, y) => {
    if (x.value === null || y.value === null) {
      if (x.value === y.value) return x.account.name.localeCompare(y.account.name);
      return x.value === null ? 1 : -1;
    }
    return y.value - x.value || x.account.name.localeCompare(y.account.name);
  });
  return { shown, hidden: list.length - shown.length };
}
