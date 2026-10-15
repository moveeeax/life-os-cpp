-- A refund of a payment that never credited the wallet (a capture that failed
-- the amount/currency check keeps its provider_capture_id with status
-- 'failed') is recorded with its own outcome and never touches the balance:
-- see Billing::refund_capture (src/billing/Wallet.hpp).
ALTER TABLE billing_refunds DROP CONSTRAINT IF EXISTS billing_refunds_outcome_check;
ALTER TABLE billing_refunds ADD CONSTRAINT billing_refunds_outcome_check
    CHECK (outcome IN ('applied', 'skipped_insufficient', 'skipped_zero_credits', 'skipped_not_credited'));
