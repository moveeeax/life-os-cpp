// What a signed-in user leaves in this browser, wiped on sign-out so the next
// person at the same device does not see it. Device choices (theme, hidden
// numbers) stay.
import { keysWhere, type ListableStore } from '@/lib/now/cache';
import { LAST_ACCOUNT_KEY } from '@/lib/ledger';
import { QUEUE_KEY } from '@/lib/workout/session';

const EXACT = new Set([LAST_ACCOUNT_KEY, QUEUE_KEY]);

const isSignedInKey = (k: string) => k.startsWith('now:') || EXACT.has(k);

function browserStore(): ListableStore | undefined {
  try {
    return globalThis.localStorage ?? undefined;
  } catch {
    return undefined;
  }
}

export function clearSignedInStorage(store: ListableStore | undefined = browserStore()): void {
  const keys = keysWhere(isSignedInKey, store);
  try {
    for (const k of keys) store?.removeItem(k);
  } catch {
    // A blocked storage holds nothing to wipe.
  }
}
