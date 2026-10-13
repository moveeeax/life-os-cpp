import { describe, expect, it } from 'vitest';

import { crossingBody, itemBody, ruleBody, tripBody } from './bodies';

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
      }),
    ).toEqual({
      name: 'Thailand',
      start_date: '2026-11-01',
      end_date: '2026-11-20',
      countries: ['TH'],
      note: '',
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
      }),
    ).toEqual({
      kind: 'flight',
      date: '2026-11-01',
      time: '23:40',
      title: 'ALA to BKK',
      reference: 'X7K2PD',
      link: '',
      note: '',
    });
  });
});
