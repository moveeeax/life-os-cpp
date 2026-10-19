// @vitest-environment jsdom
import { cleanup, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { Sheet } from '@/shell/kit';

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

const coarse = (matches: boolean) =>
  vi.stubGlobal('matchMedia', (q: string) => ({ matches: matches && q.includes('coarse') }));

describe('a sheet opens with the focus on its first field', () => {
  it('skips the Close button for the first field', () => {
    coarse(false);
    render(
      <Sheet title="New trip" onClose={() => {}}>
        <button type="button">Help</button>
        <input aria-label="Name" />
      </Sheet>,
    );
    expect(document.activeElement).toBe(screen.getByRole('textbox', { name: 'Name' }));
  });

  it('without a field, takes the first control that is not Close', () => {
    coarse(false);
    render(
      <Sheet title="Delete?" onClose={() => {}}>
        <button type="button">Cancel</button>
        <button type="button">Delete</button>
      </Sheet>,
    );
    expect(document.activeElement).toBe(screen.getByRole('button', { name: 'Cancel' }));
  });

  it('keeps a field that took the focus itself', () => {
    coarse(false);
    render(
      <Sheet title="Edit" onClose={() => {}}>
        <input aria-label="First" />
        <input aria-label="Second" autoFocus />
      </Sheet>,
    );
    expect(document.activeElement).toBe(screen.getByRole('textbox', { name: 'Second' }));
  });

  it('on a touch screen focuses the dialog, so no keyboard pops up', () => {
    coarse(true);
    render(
      <Sheet title="New trip" onClose={() => {}}>
        <input aria-label="Name" />
      </Sheet>,
    );
    expect(document.activeElement).toBe(screen.getByRole('dialog'));
  });
});
