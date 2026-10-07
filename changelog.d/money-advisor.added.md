Money module, fourth part: the advisor. A weekly written review of the past
week, on each user's chosen weekday at `MONEY_ADVISOR_HOUR_UTC` (default 7),
and on demand for this week, month or quarter. The `money_advisor` worker job
sends the LLM only numbers the server computed (the Reports page's figures,
category names, balances per currency, the user's note) with the prompt
`MONEY_LLM_PROMPT_ADVISOR`, and stores the review with those facts so every
figure can be checked; a period without rows gets one line and no provider
call; one review per user and period. Routes `GET /api/v1/money/advisor/reports`,
`GET .../{id}`, `POST /api/v1/money/advisor/run`; the Advisor tab renders the
review as plain text (headings, lists, bold) with the facts underneath. The
report building moves to `repositories/money/ReportBuilder.hpp`, shared by the
page and the job.
