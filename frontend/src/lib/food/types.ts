// Row and input shapes of /api/v1/food/*, straight from the OpenAPI spec.
import type { components } from '@/lib/api/schema.gen';

type S = components['schemas'];

export type FoodItem = S['FoodItem'];
export type FoodItemInput = S['FoodItemInput'];
export type FoodItemListResponse = S['FoodItemListResponse'];
export type FoodServing = S['FoodServing'];
export type OffProduct = S['OffProduct'];
export type FoodMeal = S['FoodMeal'];
export type FoodEntry = S['FoodEntry'];
export type FoodEntryInput = S['FoodEntryInput'];
export type FoodEntryPatch = S['FoodEntryPatch'];
export type FoodTotals = S['FoodTotals'];
export type FoodTargets = S['FoodTargets'];
export type FoodDay = S['FoodDay'];
export type FoodWeek = S['FoodWeek'];
export type FoodWeekDay = FoodWeek['days'][number];
export type FoodGoals = S['FoodGoals'];
export type FoodGoalsProfile = S['FoodGoalsProfile'];
export type FoodParseJob = S['FoodParseJob'];
export type FoodParseLine = S['FoodParseLine'];
