Money: every currency has its smallest unit. A code in the built-in table
(ISO 4217 minor units plus BTC) gets its decimals and the unit's name on
`POST /api/v1/money/currencies` (the tiyn of KZT, the kopeck of RUB, the
satoshi of BTC, 0 decimals for VND), and an amount finer than that unit is
refused with 400 on every write: transactions, receipts, transfers and fees,
opening balances, budgets. Another code gives its decimals (0..8) and may name
its unit. Migration 028 widens money amounts to eight decimals and adds
`minor_unit`; `POST /api/v1/money/rates/refresh` takes `{from, to}` and
queues one rates job per day without rates (the source starts on 2024-03-02).
