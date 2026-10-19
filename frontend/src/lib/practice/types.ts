import type { components } from '@/lib/api/schema.gen';

type S = components['schemas'];
export type PracticeAmount = S['PracticeAmount'];
export type PracticeTotal = S['PracticeTotal'];
export type PracticeClient = S['PracticeClient'];
export type PracticeClientInput = S['PracticeClientInput'];
export type PracticeDealStage = S['PracticeDealStage'];
export type PracticeDeal = S['PracticeDeal'];
export type PracticeDealInput = S['PracticeDealInput'];
export type PracticeEntry = S['PracticeEntry'];
export type PracticeEntryInput = S['PracticeEntryInput'];
export type PracticeMonth = S['PracticeMonth'];
export type PracticeClientTotal = S['PracticeClientTotal'];

export interface PracticeMonths {
  year: number;
  data: PracticeMonth[];
  totals: PracticeTotal[];
}

export interface PracticeMonthView {
  month: string;
  totals: PracticeTotal[];
  by_client: PracticeClientTotal[];
  entries: PracticeEntry[];
}
