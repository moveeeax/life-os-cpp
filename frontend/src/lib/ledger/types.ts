// Row and input shapes of /api/v1/money/*, straight from the OpenAPI spec.
import type { components } from '@/lib/api/schema.gen';

type S = components['schemas'];

export type Account = S['MoneyAccount'];
export type AccountInput = S['MoneyAccountInput'];
export type AccountPatch = S['MoneyAccountPatch'];
export type Adjustment = S['MoneyAdjustment'];
export type AsIf = S['MoneyAsIf'];
export type BalanceGroup = S['MoneyBalanceGroup'];
export type Category = S['MoneyCategory'];
export type CategoryInput = S['MoneyCategoryInput'];
export type CategoryPatch = S['MoneyCategoryPatch'];
export type CategoryTotal = S['MoneyCategoryTotal'];
export type Conversion = S['MoneyConversion'];
export type Currency = S['MoneyCurrency'];
export type CurrencyBlock = S['MoneyCurrencyBlock'];
export type CurrencyInput = S['MoneyCurrencyInput'];
export type CurrencyPatch = S['MoneyCurrencyPatch'];
export type InboxRow = S['MoneyInboxRow'];
export type Merchant = S['MoneyMerchant'];
export type ParseJob = S['MoneyParseJob'];
export type ParseLine = S['MoneyParseLine'];
export type Rate = S['MoneyRate'];
export type Recurring = S['MoneyRecurring'];
export type Report = S['MoneyReport'];
export type Settings = S['MoneySettings'];
export type SettingsInput = S['MoneySettingsInput'];
export type Transaction = S['MoneyTransaction'];
export type TransactionInput = S['MoneyTransactionInput'];
export type TransactionPatch = S['MoneyTransactionPatch'];
export type TransactionType = S['MoneyTransactionType'];
export type Transfer = S['MoneyTransfer'];
export type TransferInput = S['MoneyTransferInput'];
export type TransferPatch = S['MoneyTransferPatch'];
export type AdvisorReport = S['MoneyAdvisorReport'];
export type AdvisorReportSummary = S['MoneyAdvisorReportSummary'];
