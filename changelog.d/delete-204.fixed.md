Deleting a task, a goal, a ledger row or a transfer no longer shows "returned
an empty body" and leaves the row on screen: the API answers these with 204 No
Content, and the client now takes an empty 204 as done.
