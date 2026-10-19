import type { components } from '@/lib/api/schema.gen';

type S = components['schemas'];
export type MedicalPerson = S['MedicalPerson'];
export type MedicalPersonInput = S['MedicalPersonInput'];
export type MedicalProvider = S['MedicalProvider'];
export type MedicalProviderInput = S['MedicalProviderInput'];
export type MedicalVisit = S['MedicalVisit'];
export type MedicalVisitInput = S['MedicalVisitInput'];
export type MedicalRecord = S['MedicalRecord'];
export type MedicalRecordInput = S['MedicalRecordInput'];
export type MedicalMedication = S['MedicalMedication'];
export type MedicalMedicationInput = S['MedicalMedicationInput'];
export type MedicalTask = S['MedicalTask'];

export const RECORD_KIND: Record<MedicalRecord['kind'], string> = {
  lab: 'Lab result',
  imaging: 'Imaging',
  prescription: 'Prescription',
  discharge: 'Discharge summary',
  vaccination: 'Vaccination',
  certificate: 'Certificate',
  other: 'Other',
};

export const RECORD_KINDS = Object.keys(RECORD_KIND) as MedicalRecord['kind'][];

/** Whether a medication is still taken on @p today: no end, or an end today or later. */
export function isCurrent(m: Pick<MedicalMedication, 'end_date'>, today: string): boolean {
  return m.end_date === null || m.end_date >= today;
}

/** The medications of today first, then the finished ones; the server's order within each. */
export function splitMedications(
  list: MedicalMedication[],
  today: string,
): { current: MedicalMedication[]; past: MedicalMedication[] } {
  return {
    current: list.filter((m) => isCurrent(m, today)),
    past: list.filter((m) => !isCurrent(m, today)),
  };
}

/** Visits with a follow-up from @p today on, the soonest first. */
export function upcomingFollowUps(list: MedicalVisit[], today: string): MedicalVisit[] {
  return list
    .filter((v) => v.follow_up !== null && v.follow_up >= today)
    .sort((a, b) => (a.follow_up ?? '').localeCompare(b.follow_up ?? ''));
}

/** "Anna" for one person, "Anna, Timur" for two, "3 people" for more. */
export function peopleLabel(names: string[]): string {
  const unique = [...new Set(names)];
  if (unique.length <= 2) return unique.join(', ');
  return `${unique.length} people`;
}

/** An empty string stays out of the body; null clears. */
export function orNull(value: string): string | null {
  const v = value.trim();
  return v === '' ? null : v;
}
