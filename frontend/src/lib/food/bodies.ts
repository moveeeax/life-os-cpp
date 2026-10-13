/** The POST bodies of the add-entry sheet, as the tabs built them inline (pinned by bodies.test.ts). */
import { parseNumber } from './index';
import type { FoodMeal as Meal } from './types';

export function quickBody(i: {
  date: string;
  meal: Meal;
  name: string;
  kcal: number;
  protein: string;
  fat: string;
  carbs: string;
}) {
  return {
    date: i.date,
    meal: i.meal,
    name: i.name.trim(),
    kcal: i.kcal,
    protein_g: parseNumber(i.protein) ?? 0,
    fat_g: parseNumber(i.fat) ?? 0,
    carbs_g: parseNumber(i.carbs) ?? 0,
  };
}

export function pickBody(i: { date: string; meal: Meal; itemId: string; grams: number }) {
  return { date: i.date, meal: i.meal, item_id: i.itemId, grams: i.grams };
}
