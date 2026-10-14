import { describe, expect, it, vi } from 'vitest';
import type { KeyboardEvent } from 'react';

import { onActivateKey } from './a11y';

function key(k: string, onSelf = true): KeyboardEvent<HTMLElement> & { prevented: boolean } {
  const self = {};
  const e = {
    key: k,
    currentTarget: self,
    target: onSelf ? self : {},
    prevented: false,
    preventDefault() {
      e.prevented = true;
    },
  };
  return e as unknown as KeyboardEvent<HTMLElement> & { prevented: boolean };
}

describe('onActivateKey', () => {
  it('activates on Enter and Space, and stops Space from scrolling', () => {
    const activate = vi.fn();
    const handler = onActivateKey(activate);
    const enter = key('Enter');
    handler(enter);
    const space = key(' ');
    handler(space);
    expect(activate).toHaveBeenCalledTimes(2);
    expect(enter.prevented).toBe(true);
    expect(space.prevented).toBe(true);
  });

  it('ignores other keys', () => {
    const activate = vi.fn();
    const e = key('ArrowDown');
    onActivateKey(activate)(e);
    expect(activate).not.toHaveBeenCalled();
    expect(e.prevented).toBe(false);
  });

  it('leaves keys pressed on a control inside the element to that control', () => {
    const activate = vi.fn();
    const e = key('Enter', false);
    onActivateKey(activate)(e);
    expect(activate).not.toHaveBeenCalled();
    expect(e.prevented).toBe(false);
  });
});
