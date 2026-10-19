import { describe, expect, it } from 'vitest';

import { ApiClientError } from '@/lib/api/client';
import { checkFile, sizeLabel, storageMessage, typeOf } from '@/lib/files';

import {
  isCurrent,
  orNull,
  peopleLabel,
  splitMedications,
  upcomingFollowUps,
  type MedicalMedication,
  type MedicalVisit,
} from './index';

const med = (over: Partial<MedicalMedication>): MedicalMedication => ({
  id: 'm',
  person_id: 'p',
  person_name: 'Anna',
  name: 'Iron',
  dose: '',
  schedule: '',
  start_date: null,
  end_date: null,
  end_task_id: null,
  note: '',
  ...over,
});

const visit = (over: Partial<MedicalVisit>): MedicalVisit => ({
  id: 'v',
  person_id: 'p',
  person_name: 'Anna',
  provider_id: null,
  provider_name: null,
  date: '2026-09-01',
  reason: 'Checkup',
  outcome: '',
  follow_up: null,
  follow_up_task_id: null,
  ...over,
});

describe('medications', () => {
  it('is current without an end and through its last day', () => {
    expect(isCurrent(med({}), '2026-10-19')).toBe(true);
    expect(isCurrent(med({ end_date: '2026-10-19' }), '2026-10-19')).toBe(true);
    expect(isCurrent(med({ end_date: '2026-10-18' }), '2026-10-19')).toBe(false);
  });

  it('splits the list into now and finished in the order given', () => {
    const list = [
      med({ id: 'a' }),
      med({ id: 'b', end_date: '2026-01-01' }),
      med({ id: 'c', end_date: '2027-01-01' }),
    ];
    const { current, past } = splitMedications(list, '2026-10-19');
    expect(current.map((m) => m.id)).toEqual(['a', 'c']);
    expect(past.map((m) => m.id)).toEqual(['b']);
  });
});

describe('follow-ups', () => {
  it('keeps the ones from today on, the soonest first', () => {
    const list = [
      visit({ id: 'late', follow_up: '2027-03-01' }),
      visit({ id: 'none' }),
      visit({ id: 'past', follow_up: '2026-10-01' }),
      visit({ id: 'soon', follow_up: '2026-10-19' }),
    ];
    expect(upcomingFollowUps(list, '2026-10-19').map((v) => v.id)).toEqual(['soon', 'late']);
  });
});

describe('labels', () => {
  it('names one or two people and counts more', () => {
    expect(peopleLabel(['Anna', 'Anna'])).toBe('Anna');
    expect(peopleLabel(['Anna', 'Timur'])).toBe('Anna, Timur');
    expect(peopleLabel(['Anna', 'Timur', 'Olesia'])).toBe('3 people');
  });

  it('turns an empty field into null', () => {
    expect(orNull('  ')).toBeNull();
    expect(orNull(' 2026-10-01 ')).toBe('2026-10-01');
  });
});

describe('files', () => {
  it('takes PDFs and photos up to 25 MB', () => {
    expect(checkFile({ type: 'application/pdf', name: 'a.pdf', size: 10 })).toEqual({
      ok: true,
      type: 'application/pdf',
    });
    expect(checkFile({ type: 'application/zip', name: 'a.zip', size: 10 }).ok).toBe(false);
    expect(checkFile({ type: 'image/jpeg', name: 'a.jpg', size: 0 }).ok).toBe(false);
    expect(checkFile({ type: 'image/jpeg', name: 'a.jpg', size: 26 * 1024 * 1024 }).ok).toBe(false);
  });

  it('reads a HEIC photo the browser gave no type by its name', () => {
    expect(typeOf({ type: '', name: 'IMG_1.HEIC' })).toBe('image/heic');
    expect(typeOf({ type: '', name: 'notes.txt' })).toBeNull();
  });

  it('labels sizes', () => {
    expect(sizeLabel(300)).toBe('1 KB');
    expect(sizeLabel(340 * 1024)).toBe('340 KB');
    expect(sizeLabel(1.25 * 1024 * 1024)).toBe('1.3 MB');
  });

  it('says what the storage answered', () => {
    const e = (code: string) => new ApiClientError({ status: 503, message: 'x', code });
    expect(storageMessage(e('storage_not_configured'))).toMatch(/not set up/);
    expect(storageMessage(e('storage_public'))).toMatch(/anyone/);
    expect(storageMessage(e('storage_unavailable'))).toMatch(/refused/);
    expect(storageMessage(new ApiClientError({ status: 404, message: 'x' }))).toBeNull();
    expect(storageMessage(new Error('x'))).toBeNull();
  });
});
