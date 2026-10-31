/**
 * What the person just did on the site, for Ani to react to (her corner
 * listens while she is on screen; pages call act() after a write that went
 * through). The bus is a window event, so a page needs no import of Now.
 */
export const ACT_EVENT = 'life:act';

export type Act =
  | { kind: 'task_done'; title: string; at: number }
  | {
      kind: 'money_added';
      name: string;
      merchant: string | null;
      amount: number;
      currency: string;
      at: number;
    }
  | { kind: 'habit_ticked'; at: number }
  | { kind: 'food_added'; name: string; at: number }
  | { kind: 'goal_done'; title: string; at: number }
  | { kind: 'workout_started'; at: number }
  | { kind: 'note_opened'; title: string; at: number };

type Without<T> = T extends unknown ? Omit<T, 'at'> : never;

/** Tells Ani what was done; quiet outside a browser. */
export function act(a: Without<Act>): void {
  if (typeof window === 'undefined') return;
  window.dispatchEvent(new CustomEvent(ACT_EVENT, { detail: { ...a, at: Date.now() } }));
}
