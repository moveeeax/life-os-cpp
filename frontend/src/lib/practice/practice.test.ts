import { describe, expect, it } from 'vitest';

import {
  amountsLine,
  byMonth,
  currencyDigits,
  dealBody,
  dealDates,
  dealsByStage,
  entryBody,
  entryTitle,
  money,
  monthLabel,
  netLine,
  type DealForm,
  type EntryForm,
  type PracticeDeal,
  type PracticeEntry,
} from './index';

const deal = (over: Partial<PracticeDeal>): PracticeDeal => ({
  id: 'd1',
  client_id: 'c1',
  client_name: 'Acme',
  title: 'Landing page',
  stage: 'active',
  value: 1500,
  currency: 'USD',
  start_date: null,
  end_date: null,
  note: '',
  open_tasks: 0,
  received: [],
  created_at: '2026-10-01T00:00:00+00:00',
  updated_at: '2026-10-01T00:00:00+00:00',
  ...over,
});

const entry = (over: Partial<PracticeEntry>): PracticeEntry => ({
  id: 'e1',
  kind: 'income',
  date: '2026-10-03',
  amount: 1000,
  currency: 'USD',
  client_id: null,
  client_name: null,
  deal_id: null,
  deal_title: null,
  note: '',
  created_at: '2026-10-03T00:00:00+00:00',
  updated_at: '2026-10-03T00:00:00+00:00',
  ...over,
});

describe('amounts', () => {
  it('prints an amount in its own currency, cents only when there are any', () => {
    expect(money(1500, 'USD')).toBe('1,500 USD');
    expect(money(1500.5, 'USD')).toBe('1,500.50 USD');
    expect(money(250000, 'KZT')).toBe('250,000 KZT');
    expect(money(-12.5, 'EUR', true)).toBe('−12.50 EUR');
    expect(money(980, 'USD', true)).toBe('+980 USD');
  });

  it('knows the decimals of a currency', () => {
    expect(currencyDigits('USD')).toBe(2);
    expect(currencyDigits('JPY')).toBe(0);
    expect(currencyDigits('KWD')).toBe(3);
    expect(currencyDigits('??')).toBe(2);
  });

  it('puts currencies side by side and never adds them', () => {
    const line = amountsLine([
      { currency: 'KZT', amount: 250000 },
      { currency: 'USD', amount: 1000 },
    ]);
    expect(line).toBe('250,000 KZT · 1,000 USD');
    expect(
      netLine([
        { currency: 'EUR', net: -12.5 },
        { currency: 'USD', net: 980 },
      ]),
    ).toBe('−12.50 EUR · +980 USD');
  });
});

describe('labels', () => {
  it('names a month', () => {
    expect(monthLabel('2026-10')).toBe('October 2026');
    expect(monthLabel('2027-01')).toBe('January 2027');
  });

  it('titles an entry by its note, deal, client, then kind', () => {
    expect(entryTitle(entry({ note: 'Invoice 12', deal_title: 'Site' }))).toBe('Invoice 12');
    expect(entryTitle(entry({ deal_title: 'Site', client_name: 'Acme' }))).toBe('Site');
    expect(entryTitle(entry({ client_name: 'Acme' }))).toBe('Acme');
    expect(entryTitle(entry({ kind: 'expense' }))).toBe('Expense');
  });

  it('words the dates of a deal', () => {
    const today = '2026-10-19';
    expect(dealDates(deal({}), today)).toBe('');
    expect(dealDates(deal({ start_date: '2026-10-01', end_date: '2026-11-30' }), today)).toBe(
      'Oct 1 – Nov 30',
    );
    expect(dealDates(deal({ start_date: '2026-10-01' }), today)).toBe('from Oct 1');
    expect(dealDates(deal({ end_date: '2027-01-15' }), today)).toBe('until Jan 15, 2027');
  });
});

describe('grouping', () => {
  it('groups deals by stage, open stages first, empty stages left out', () => {
    const groups = dealsByStage([
      deal({ id: 'a', stage: 'done' }),
      deal({ id: 'b', stage: 'lead' }),
      deal({ id: 'c', stage: 'active' }),
      deal({ id: 'd', stage: 'lead' }),
    ]);
    expect(groups.map((g) => [g.stage, g.deals.map((d) => d.id)])).toEqual([
      ['lead', ['b', 'd']],
      ['active', ['c']],
      ['done', ['a']],
    ]);
  });

  it('groups entries by month in the order they come', () => {
    const groups = byMonth([
      entry({ id: '1', date: '2026-10-05' }),
      entry({ id: '2', date: '2026-10-01' }),
      entry({ id: '3', date: '2026-09-30' }),
    ]);
    expect(groups.map((g) => [g.month, g.rows.map((r) => r.id)])).toEqual([
      ['2026-10', ['1', '2']],
      ['2026-09', ['3']],
    ]);
  });
});

describe('form bodies', () => {
  const dealForm: DealForm = {
    client_id: 'c1',
    title: ' Landing page ',
    stage: 'lead',
    value: '',
    currency: 'usd',
    start_date: '',
    end_date: '',
    note: '',
  };

  it('sends a deal without a value as null value and null currency', () => {
    expect(dealBody(dealForm)).toEqual({
      body: {
        client_id: 'c1',
        title: 'Landing page',
        stage: 'lead',
        value: null,
        currency: null,
        start_date: null,
        end_date: null,
        note: '',
      },
    });
  });

  it('sends a value with its currency in capitals', () => {
    const out = dealBody({ ...dealForm, value: '1500,50' });
    expect(out).toMatchObject({ body: { value: 1500.5, currency: 'USD' } });
  });

  it('refuses a bad value, a missing currency and an end before the start', () => {
    expect(dealBody({ ...dealForm, value: 'abc' })).toHaveProperty('error');
    expect(dealBody({ ...dealForm, value: '100', currency: '' })).toHaveProperty('error');
    expect(
      dealBody({ ...dealForm, start_date: '2026-10-10', end_date: '2026-10-01' }),
    ).toHaveProperty('error');
  });

  const entryForm: EntryForm = {
    kind: 'income',
    date: '2026-10-03',
    amount: '1000',
    currency: 'usd',
    client_id: 'c1',
    deal_id: '',
    note: ' Invoice 12 ',
  };

  it('sends an entry; a deal brings its client', () => {
    expect(entryBody(entryForm)).toEqual({
      body: {
        kind: 'income',
        date: '2026-10-03',
        amount: 1000,
        currency: 'USD',
        client_id: 'c1',
        deal_id: null,
        note: 'Invoice 12',
      },
    });
    expect(entryBody({ ...entryForm, deal_id: 'd1' })).toMatchObject({
      body: { client_id: null, deal_id: 'd1' },
    });
  });

  it('refuses an entry without a positive amount or a currency code', () => {
    expect(entryBody({ ...entryForm, amount: '0' })).toHaveProperty('error');
    expect(entryBody({ ...entryForm, amount: '' })).toHaveProperty('error');
    expect(entryBody({ ...entryForm, currency: 'US' })).toHaveProperty('error');
  });
});
