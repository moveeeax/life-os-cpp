Money module, fifth part: the one-off import from Notion. `POST
/api/v1/money/import/notion` loads the JSON of `scripts/notion-money-export.py`
(currencies, accounts, categories, transactions, transfers; rows refer to
each other by their Notion page id) in one database transaction; a second run
updates the same rows instead of adding them again, and a row that breaks a
rule of the model is rolled back alone and listed with the reason. The script
reads the five databases through the Notion API or from a dump and fixes on
the way what Notion allowed: an empty category kind becomes expense, a budget
without a currency is dropped, times move from UTC to where the owner was;
everything it changed is listed in `notes`. `POST /api/v1/money/rates/refresh`
takes `{from, to}` and queues one rates job per day without rates (the source
starts on 2024-03-02). Migration 028: `external_id` on accounts and
categories, transfer amounts with eight decimals and currencies with up to
eight (a BTC wallet), a wider rate column.
