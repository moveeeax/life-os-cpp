Food module, second part: a text description of a meal is parsed by an LLM
into diary lines. The worker job `food_parse` speaks the OpenAI-compatible
chat completions API to whatever `FOOD_LLM_BASE_URL`, `FOOD_LLM_API_KEY`,
`FOOD_LLM_MODEL` and `FOOD_LLM_PROMPT` name; the answer is checked against a
schema (an item id must be the user's own) and waits in the job for the
user's confirmation. `POST /api/v1/food/parse`, `GET /api/v1/food/parse/{id}`;
`GET /api/v1/food/goals` reports `llm_available`.
