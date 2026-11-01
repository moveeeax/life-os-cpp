import { describe, expect, it } from 'vitest';

import { keepsNow } from './wide';

describe('keepsNow', () => {
  it('lets the shell own Now on a wide screen unless the module is known to be off', () => {
    expect(keepsNow(true, true)).toBe(true);
    // The modules probe is still out: the shell already draws Now, so the
    // route does not mount a first copy that is torn down when it answers.
    expect(keepsNow(true, undefined)).toBe(true);
    expect(keepsNow(true, false)).toBe(false);
    expect(keepsNow(false, true)).toBe(false);
    expect(keepsNow(false, undefined)).toBe(false);
  });
});
