import type { components } from '@/lib/api/schema.gen';

type S = components['schemas'];
export type Task = S['Task'];
export type TaskInput = S['TaskInput'];
export type TaskPatch = S['TaskPatch'];
export type TaskAgenda = S['TaskAgenda'];
export type TaskNote = S['TaskNote'];
export type TaskParseJob = S['TaskParseJob'];
export type TaskParseLine = S['TaskParseLine'];
export type Area = Task['area'];
export type Effort = NonNullable<Task['effort']>;
