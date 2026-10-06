Money module, first part (off by default, `MONEY_ENABLED`): currencies,
accounts with computed balances, categories with a budget in one currency,
the ledger of incomes, expenses and the bank's signed fx adjustments,
transfers and exchanges as one row with the amounts sent and received,
an inbox of pending rows with a duplicate flag, merchant memory, period
reports per currency (never summed across) with the previous period, the
median of three, the fixed share, the projection, recurring charges and
new merchants, and an optional "as if" block converted at a stored daily
rate. Rates come from fawazahmed0/currency-api through the `money_rates`
worker job (`MONEY_RATES_SCHEDULE_HOURS`). Every page action is a route
under `/api/v1/money/`, so an agent with an API key can do the same.
