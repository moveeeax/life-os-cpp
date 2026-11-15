/** The bodies of the medical forms (pinned by bodies.test.ts). */
import {
  orNull,
  type MedicalMedicationInput,
  type MedicalPersonInput,
  type MedicalProvider,
  type MedicalProviderInput,
  type MedicalRecord,
  type MedicalRecordInput,
  type MedicalVisitInput,
} from './index';

export interface PersonForm {
  name: string;
  relation: string;
  note: string;
}

export function personBody(f: PersonForm): MedicalPersonInput {
  return { name: f.name.trim(), relation: f.relation.trim(), note: f.note.trim() };
}

export interface ProviderForm {
  kind: MedicalProvider['kind'];
  name: string;
  specialty: string;
  clinic: string;
  phone: string;
  email: string;
  address: string;
  note: string;
}

/** A clinic has no clinic of its own: the field goes out empty. */
export function providerBody(f: ProviderForm): MedicalProviderInput {
  return {
    kind: f.kind,
    name: f.name.trim(),
    specialty: f.specialty.trim(),
    clinic: f.kind === 'doctor' ? f.clinic.trim() : '',
    phone: f.phone.trim(),
    email: f.email.trim(),
    address: f.address.trim(),
    note: f.note.trim(),
  };
}

export interface VisitForm {
  person_id: string;
  provider_id: string;
  date: string;
  reason: string;
  outcome: string;
  follow_up: string;
}

export function visitBody(f: VisitForm): MedicalVisitInput {
  return {
    person_id: f.person_id,
    provider_id: orNull(f.provider_id),
    date: f.date,
    reason: f.reason.trim(),
    outcome: f.outcome.trim(),
    follow_up: orNull(f.follow_up),
  };
}

export interface RecordForm {
  person_id: string;
  visit_id: string;
  date: string;
  kind: MedicalRecord['kind'];
  title: string;
  note: string;
}

export function recordBody(f: RecordForm): MedicalRecordInput {
  return {
    person_id: f.person_id,
    visit_id: orNull(f.visit_id),
    date: f.date,
    kind: f.kind,
    title: f.title.trim(),
    note: f.note.trim(),
  };
}

export interface MedicationForm {
  person_id: string;
  name: string;
  dose: string;
  schedule: string;
  start_date: string;
  end_date: string;
  note: string;
}

export function medicationBody(f: MedicationForm): MedicalMedicationInput {
  return {
    person_id: f.person_id,
    name: f.name.trim(),
    dose: f.dose.trim(),
    schedule: f.schedule.trim(),
    start_date: orNull(f.start_date),
    end_date: orNull(f.end_date),
    note: f.note.trim(),
  };
}
