import { describe, expect, it } from 'vitest';

import { contactBody, crossingBody, itemBody, momentBody, ruleBody, tripBody } from './bodies';

describe('travel bodies', () => {
  it('a crossing trims and drops an empty time', () => {
    expect(
      crossingBody({
        date: '2026-11-01',
        from: 'kz',
        to: 'th',
        time: '',
        place: ' ALA ',
        note: '',
      }),
    ).toEqual({
      date: '2026-11-01',
      from: 'KZ',
      to: 'TH',
      time: null,
      place: 'ALA',
      note: '',
    });
  });
  it('a rule sends window days only for a rolling window', () => {
    expect(
      ruleBody({
        name: ' Visa ',
        countries: 'th, la',
        window_kind: 'since_last_entry',
        window_days: '180',
        threshold_days: '60',
        threshold_kind: 'at_most',
        count_crossing_days: true,
        source: '',
      }),
    ).toEqual({
      name: 'Visa',
      countries: ['TH', 'LA'],
      window_kind: 'since_last_entry',
      window_days: null,
      threshold_days: 60,
      threshold_kind: 'at_most',
      count_crossing_days: true,
      source: '',
    });
    expect(
      ruleBody({
        name: 'S',
        countries: 'SCHENGEN',
        window_kind: 'rolling_days',
        window_days: '180',
        threshold_days: '90',
        threshold_kind: 'at_most',
        count_crossing_days: true,
        source: '',
      }).window_days,
    ).toBe(180);
  });
  it('a trip and an item', () => {
    expect(
      tripBody({
        name: ' Thailand ',
        start_date: '2026-11-01',
        end_date: '2026-11-20',
        countries: 'th',
        note: '',
        purpose: '',
        booked_amount: '',
        booked_currency: '',
      }),
    ).toEqual({
      name: 'Thailand',
      start_date: '2026-11-01',
      end_date: '2026-11-20',
      countries: ['TH'],
      note: '',
      purpose: null,
      booked_cost: null,
    });
    expect(
      itemBody({
        kind: 'flight',
        date: '2026-11-01',
        time: '23:40',
        title: ' ALA to BKK ',
        reference: 'X7K2PD',
        link: '',
        note: '',
        no_crossing: true,
        end_date: '2026-11-03',
      }),
    ).toEqual({
      kind: 'flight',
      date: '2026-11-01',
      time: '23:40',
      title: 'ALA to BKK',
      reference: 'X7K2PD',
      link: '',
      note: '',
      no_crossing: true,
      end_date: null,
    });
  });
  it('only a flight keeps the no-crossing mark', () => {
    const stay = itemBody({
      kind: 'stay',
      date: '2026-11-02',
      time: '',
      title: 'Lanta Sand',
      reference: '',
      link: '',
      note: '',
      no_crossing: true,
      end_date: '2026-11-09',
    });
    expect(stay.no_crossing).toBe(false);
    expect(stay.end_date).toBe('2026-11-09');
  });
  it('a trip sends its purpose and its booked cost in its own currency', () => {
    const base = {
      name: 'Bangkok',
      start_date: '2026-11-01',
      end_date: '2026-11-20',
      countries: '',
      note: '',
    };
    const body = tripBody({
      ...base,
      purpose: 'relocation',
      booked_amount: ' 1250,50 ',
      booked_currency: 'thb',
    });
    expect(body.purpose).toBe('relocation');
    expect(body.booked_cost).toEqual({ amount: 1250.5, currency: 'THB' });
    // A half-filled cost goes as it is, so the API names the missing half instead of saving 0.
    const half = tripBody({ ...base, purpose: '', booked_amount: '', booked_currency: 'USD' });
    expect(half.booked_cost?.amount).toBeNaN();
  });
  it('a contact and a moment trim their text', () => {
    expect(
      contactBody({
        name: ' Somchai ',
        role: 'host ',
        phone: ' +66 81 234 5678',
        email: '',
        link: '',
        note: '',
      }),
    ).toEqual({
      name: 'Somchai',
      role: 'host',
      phone: '+66 81 234 5678',
      email: '',
      link: '',
      note: '',
    });
    expect(momentBody({ date: '2026-11-03', text: ' Sunset ', place: '' })).toEqual({
      date: '2026-11-03',
      text: 'Sunset',
      place: '',
    });
  });
});
