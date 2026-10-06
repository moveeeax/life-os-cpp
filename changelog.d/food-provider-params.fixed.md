The `food_parse` job adapts its request to the provider: a 400 that names
`max_tokens` (OpenAI's reasoning models want `max_completion_tokens`),
`temperature` (only the default is allowed there) or `response_format` is
answered by renaming or dropping that parameter and trying again, one
parameter per round. The provider's message is stored with the error code
and shown on the page.
