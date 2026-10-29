// @vitest-environment jsdom
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { FocusStart } from './FocusStart';

/** The focus length is one pressed button, remembered, and Focus starts with it. */

beforeEach(() => localStorage.clear());
afterEach(cleanup);

describe('FocusStart', () => {
  it('presses exactly one length, 25 by default', () => {
    render(<FocusStart onStart={() => {}} />);
    const pressed = screen.getAllByRole('button', { pressed: true }).map((b) => b.textContent);
    expect(pressed).toEqual(['25 min']);
  });

  it('starts with the picked length and remembers it', () => {
    const onStart = vi.fn();
    render(<FocusStart onStart={onStart} />);
    fireEvent.click(screen.getByRole('button', { name: '90 min' }));
    expect(screen.getByRole('button', { name: '90 min' }).getAttribute('aria-pressed')).toBe(
      'true',
    );
    expect(screen.getByRole('button', { name: '25 min' }).getAttribute('aria-pressed')).toBe(
      'false',
    );
    fireEvent.click(screen.getByRole('button', { name: 'Focus' }));
    expect(onStart).toHaveBeenCalledWith(90);
    expect(localStorage.getItem('now:focus:length')).toBe('90');
  });
});
