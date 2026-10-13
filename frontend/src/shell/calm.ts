/**
 * Sections already on the calm sheet (docs/superpowers/specs/2026-10-13-sections-glass-design.md).
 * The shell renders their routes without the legacy sheet; each section's
 * move adds its prefix here.
 */
export const CALM_PREFIXES: readonly string[] = [];

export function matchesPrefix(pathname: string, prefixes: readonly string[]): boolean {
  return prefixes.some((p) => pathname === p || pathname.startsWith(`${p}/`));
}

export function isCalm(pathname: string): boolean {
  return matchesPrefix(pathname, CALM_PREFIXES);
}
