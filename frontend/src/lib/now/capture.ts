/** The amount of a parsed spend line with its account's currency, and the account's name. */
export function spendFacts(
  line: { amount: string; account_id: string | null },
  accounts: { id: string; name: string; currency: string }[],
): { amount: string; account: string } {
  const account = accounts.find((a) => a.id === line.account_id);
  return {
    amount: account ? `${line.amount} ${account.currency}` : line.amount,
    account: account ? account.name : 'no account',
  };
}
