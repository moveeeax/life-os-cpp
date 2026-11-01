import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { MEDICAL_PROBE } from '@/hooks/moduleProbes';
import { useModuleOn } from '@/hooks/useModuleProbe';
import { api } from '@/lib/api/client';
import { NOW_RIBBON, qk } from '@/lib/api/queryKeys';
import type {
  MedicalMedication,
  MedicalMedicationInput,
  MedicalPerson,
  MedicalPersonInput,
  MedicalProvider,
  MedicalProviderInput,
  MedicalRecord,
  MedicalRecordInput,
  MedicalTask,
  MedicalVisit,
  MedicalVisitInput,
} from '@/lib/medical';

const BASE = '/api/v1/medical';

/** A medical write moves the archive; a task made from it moves Tasks and Now too. */
const WRITES = [qk.medical.all()] as const;
const TASK_WRITES = [qk.medical.all(), qk.tasks.all(), ...NOW_RIBBON] as const;

/** Whether the medical module is on (undefined while asked). */
export function useMedicalOn(enabled = true): boolean | undefined {
  return useModuleOn('medical', MEDICAL_PROBE, enabled);
}

function list<T>(key: readonly unknown[], path: string) {
  return {
    queryKey: key,
    queryFn: async ({ signal }: { signal: AbortSignal }) =>
      (await api.getJson<{ data: T[] }>(`${BASE}/${path}`, { signal })).data,
  };
}

export const usePeople = () => useQuery(list<MedicalPerson>(qk.medical.people(), 'people'));
export const useProviders = () =>
  useQuery(list<MedicalProvider>(qk.medical.providers(), 'providers'));
export const useVisits = () => useQuery(list<MedicalVisit>(qk.medical.visits(), 'visits'));
export const useRecords = () => useQuery(list<MedicalRecord>(qk.medical.records(), 'records'));
export const useMedications = () =>
  useQuery(list<MedicalMedication>(qk.medical.medications(), 'medications'));

function useSave<I, T>(path: string, id: string | null, onSuccess?: (row: T) => void) {
  return useApiMutation(
    async (body: I) =>
      id
        ? (await api.patchJson<{ data: T }>(`${BASE}/${path}/${id}`, { body })).data
        : (await api.postJson<{ data: T }>(`${BASE}/${path}`, { body })).data,
    { invalidate: WRITES, onSuccess },
  );
}

function useRemove(path: string, onSuccess?: () => void) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${BASE}/${path}/${id}`), {
    invalidate: WRITES,
    onSuccess,
  });
}

export const useSavePerson = (id: string | null, onSuccess?: (p: MedicalPerson) => void) =>
  useSave<MedicalPersonInput, MedicalPerson>('people', id, onSuccess);
export const useDeletePerson = (onSuccess?: () => void) => useRemove('people', onSuccess);
export const useSaveProvider = (id: string | null, onSuccess?: (p: MedicalProvider) => void) =>
  useSave<MedicalProviderInput, MedicalProvider>('providers', id, onSuccess);
export const useDeleteProvider = (onSuccess?: () => void) => useRemove('providers', onSuccess);
export const useSaveVisit = (id: string | null, onSuccess?: (v: MedicalVisit) => void) =>
  useSave<MedicalVisitInput, MedicalVisit>('visits', id, onSuccess);
export const useDeleteVisit = (onSuccess?: () => void) => useRemove('visits', onSuccess);
export const useSaveRecord = (id: string | null, onSuccess?: (r: MedicalRecord) => void) =>
  useSave<MedicalRecordInput, MedicalRecord>('records', id, onSuccess);
export const useDeleteRecord = (onSuccess?: () => void) => useRemove('records', onSuccess);
export const useSaveMedication = (id: string | null, onSuccess?: (m: MedicalMedication) => void) =>
  useSave<MedicalMedicationInput, MedicalMedication>('medications', id, onSuccess);
export const useDeleteMedication = (onSuccess?: () => void) => useRemove('medications', onSuccess);

/** POST .../{id}/task: the task of a follow-up date or of a medication end. */
export function useDateTask(path: 'visits' | 'medications') {
  return useApiMutation(
    async (id: string) =>
      (
        await api.postJson<{ data: { task: MedicalTask; created: boolean } }>(
          `${BASE}/${path}/${id}/task`,
        )
      ).data,
    { invalidate: TASK_WRITES },
  );
}
