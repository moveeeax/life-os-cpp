Money module, second part: a bank mail, an SMS, a list of purchases or a
receipt photo becomes proposed ledger lines through the `money_parse` worker
job. The job sends the user's accounts (with the card's last four digits),
categories and remembered merchants to the OpenAI-compatible API named by
`MONEY_LLM_*` (its own ConfigMap and Secret `life-os-money-llm`); an account
or category the model names that is not the user's is dropped with a note.
`POST /api/v1/money/parse`, `POST /api/v1/money/parse/receipt` (a data URL,
4 MB), `GET /api/v1/money/parse/{id}`, and `POST .../accept`, which puts the
edited lines into the inbox as pending rows, once. The LLM call is shared
with the food parse (`src/llm/Chat.hpp`).
