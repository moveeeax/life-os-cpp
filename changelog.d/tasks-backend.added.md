Tasks: a new module (`TASKS_ENABLED`) for personal tasks. `/api/v1/tasks/items`
keeps tasks with one of six areas, an effort, a due date, a next step and a
source (a link or a money ledger row of the same user); `GET
/api/v1/tasks/agenda?date=&tz=` groups them into late, the next two days,
dated, someday, done today and two review lists, reading the day in the
caller's time zone; `/api/v1/tasks/notes` is the inbox; `POST
/api/v1/tasks/parse` turns one phrase into drafts through the `tasks_parse`
job, which uses the money LLM provider with its own prompt
(`TASKS_LLM_PROMPT_PARSE`, ConfigMap `life-os-tasks-llm`). Migration 029.
