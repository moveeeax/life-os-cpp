import type { components } from '@/lib/api/schema.gen';

type S = components['schemas'];
export type Habit = S['Habit'];
export type HabitInput = S['HabitInput'];
export type HabitDay = S['HabitDay'];
export type HabitDays = S['HabitDays'];
export type HabitCheck = S['HabitCheck'];
