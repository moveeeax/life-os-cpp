// Row and input shapes of /api/v1/workout/*, straight from the OpenAPI spec.
import type { components } from '@/lib/api/schema.gen';

type S = components['schemas'];

export type Exercise = S['Exercise'];
export type ExerciseInput = S['ExerciseInput'];
export type ExerciseListResponse = S['ExerciseListResponse'];
export type TrackingMode = S['ExerciseTrackingMode'];
export type Routine = S['Routine'];
export type RoutineExercise = S['RoutineExercise'];
export type RoutineSummary = S['RoutineSummary'];
export type RoutineInput = S['RoutineInput'];
