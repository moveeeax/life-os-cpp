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

export type WorkoutSession = S['WorkoutSession'];
export type WorkoutSessionSummary = S['WorkoutSessionSummary'];
export type WorkoutSessionExercise = S['WorkoutSessionExercise'];
export type WorkoutSessionPatch = S['WorkoutSessionPatch'];
export type WorkoutSet = S['WorkoutSet'];
export type WorkoutPreviousSet = S['WorkoutPreviousSet'];
export type WorkoutSetInput = S['WorkoutSetInput'];
export type WorkoutHealthStatus = S['WorkoutHealthStatus'];
export type WorkoutSessionListResponse = S['WorkoutSessionListResponse'];
export type WorkoutHeartRate = S['WorkoutHeartRateResponse']['data'];
export type WorkoutReadiness = S['WorkoutReadinessResponse']['data'];
