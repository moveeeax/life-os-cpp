// The [query key, url] probe of each module: a cheap GET that 404s while the
// module is switched off. GET /api/v1/modules answers for all of them at once;
// a probe runs only when that call fails. Kept apart from the module hooks so
// the shell can read them without loading every section's code.
import { qk } from '@/lib/api/queryKeys';

export const NOW_PROBE = [qk.now.enabled(), '/api/v1/wallpapers?device=desktop'] as const;
export const WORKOUT_PROBE = [qk.workout.enabled(), '/api/v1/workout/sessions/active'] as const;
export const FOOD_PROBE = [qk.food.enabled(), '/api/v1/food/goals'] as const;
export const MONEY_PROBE = [qk.money.enabled(), '/api/v1/money/settings'] as const;
export const TASKS_PROBE = [qk.tasks.enabled(), '/api/v1/tasks/status'] as const;
export const GOALS_PROBE = [qk.goals.enabled(), '/api/v1/goals?status=active'] as const;
export const TRAVEL_PROBE = [qk.travel.enabled(), '/api/v1/travel/settings'] as const;
export const HABITS_PROBE = [qk.habits.enabled(), '/api/v1/habits'] as const;
export const NOTES_PROBE = [qk.notes.enabled(), '/api/v1/notes?limit=1'] as const;
export const MEDICAL_PROBE = [qk.medical.enabled(), '/api/v1/medical/people'] as const;
