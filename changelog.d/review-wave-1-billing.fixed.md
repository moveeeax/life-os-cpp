A refund of a payment that never credited the wallet (a capture that failed
the amount or currency check keeps its capture id with status `failed`) is
recorded with the outcome `skipped_not_credited` and no longer debits the
credits that other payments put on the balance (migration 041 widens the
`billing_refunds.outcome` check).
