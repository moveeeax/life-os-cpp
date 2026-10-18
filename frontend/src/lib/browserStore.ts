/**
 * The browser's localStorage, or undefined when it is absent or blocked (a
 * locked down browser throws on the access itself; a render must not).
 */
export function browserStore(): Storage | undefined {
  try {
    return globalThis.localStorage ?? undefined;
  } catch {
    return undefined;
  }
}
