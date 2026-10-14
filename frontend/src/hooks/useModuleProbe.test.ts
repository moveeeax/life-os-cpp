import { describe, expect, it } from 'vitest';

import { probesSettled } from './useModuleProbe';

describe('probesSettled', () => {
  it('waits for every enabled probe', () => {
    expect(
      probesSettled([
        { enabled: true, on: true },
        { enabled: true, on: undefined },
      ]),
    ).toBe(false);
  });

  it('a probe that answered off counts as settled', () => {
    expect(
      probesSettled([
        { enabled: true, on: false },
        { enabled: true, on: true },
      ]),
    ).toBe(true);
  });

  it('a probe that will not run does not hold the others', () => {
    expect(
      probesSettled([
        { enabled: false, on: undefined },
        { enabled: true, on: true },
      ]),
    ).toBe(true);
  });
});
