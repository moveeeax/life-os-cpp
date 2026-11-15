import { describe, expect, it } from 'vitest';

import { medicationBody, personBody, providerBody, recordBody, visitBody } from './bodies';

describe('medical bodies', () => {
  it('a person: every text field trimmed', () => {
    expect(personBody({ name: ' Anna ', relation: ' wife ', note: ' A+ ' })).toEqual({
      name: 'Anna',
      relation: 'wife',
      note: 'A+',
    });
  });

  it('a provider: a doctor keeps the clinic, a clinic sends it empty', () => {
    const f = {
      kind: 'doctor' as const,
      name: ' Dr Lee ',
      specialty: ' ENT ',
      clinic: ' City ',
      phone: ' 1 ',
      email: ' a@b.c ',
      address: ' Main 1 ',
      note: ' ok ',
    };
    expect(providerBody(f)).toEqual({
      kind: 'doctor',
      name: 'Dr Lee',
      specialty: 'ENT',
      clinic: 'City',
      phone: '1',
      email: 'a@b.c',
      address: 'Main 1',
      note: 'ok',
    });
    expect(providerBody({ ...f, kind: 'clinic' }).clinic).toBe('');
  });

  it('a visit: an empty provider and follow-up go out as null', () => {
    expect(
      visitBody({
        person_id: 'p',
        provider_id: '',
        date: '2026-11-15',
        reason: ' Checkup ',
        outcome: ' fine ',
        follow_up: '  ',
      }),
    ).toEqual({
      person_id: 'p',
      provider_id: null,
      date: '2026-11-15',
      reason: 'Checkup',
      outcome: 'fine',
      follow_up: null,
    });
    expect(
      visitBody({
        person_id: 'p',
        provider_id: 'd',
        date: '2026-11-15',
        reason: 'x',
        outcome: '',
        follow_up: '2026-12-01',
      }),
    ).toMatchObject({ provider_id: 'd', follow_up: '2026-12-01' });
  });

  it('a record: an empty visit is null, the title and note trimmed', () => {
    expect(
      recordBody({
        person_id: 'p',
        visit_id: '',
        date: '2026-11-15',
        kind: 'lab',
        title: ' CBC ',
        note: ' fasting ',
      }),
    ).toEqual({
      person_id: 'p',
      visit_id: null,
      date: '2026-11-15',
      kind: 'lab',
      title: 'CBC',
      note: 'fasting',
    });
  });

  it('a medication: empty dates are null', () => {
    expect(
      medicationBody({
        person_id: 'p',
        name: ' Iron ',
        dose: ' 1 pill ',
        schedule: ' daily ',
        start_date: '',
        end_date: '2026-12-01',
        note: '',
      }),
    ).toEqual({
      person_id: 'p',
      name: 'Iron',
      dose: '1 pill',
      schedule: 'daily',
      start_date: null,
      end_date: '2026-12-01',
      note: '',
    });
  });
});
