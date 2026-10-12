`GET /api/v1/money/summary?currency=USD&days=90` gives all Money accounts in
one currency at the day's rates: the total with each account's rate and rate
day, money without a rate listed apart (the total then says it is partial),
the total of every day of the period and the change split into what the ledger
did and what the rates did. It answers to an API key too. `/api/v1/today`
carries the same total in USD as `money.net_worth`. The rates job always keeps
RUB with USD, EUR and KZT.
