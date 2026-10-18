// @vitest-environment jsdom
import { act, cleanup, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';

import { useHideNumbers } from './useHideNumbers';

function Probe({ name }: { name: string }) {
  const [hidden, setHidden] = useHideNumbers();
  return (
    <button type="button" data-testid={name} onClick={() => setHidden(!hidden)}>
      {hidden ? 'hidden' : 'shown'}
    </button>
  );
}

afterEach(() => {
  cleanup();
});

describe('useHideNumbers', () => {
  it('keeps every caller on one state', () => {
    render(
      <>
        <Probe name="pill" />
        <Probe name="menu" />
      </>,
    );
    const start = screen.getByTestId('pill').textContent;
    expect(screen.getByTestId('menu').textContent).toBe(start);
    act(() => screen.getByTestId('pill').click());
    const flipped = start === 'hidden' ? 'shown' : 'hidden';
    expect(screen.getByTestId('pill').textContent).toBe(flipped);
    expect(screen.getByTestId('menu').textContent).toBe(flipped);
    expect(document.documentElement.classList.contains('hide-numbers')).toBe(flipped === 'hidden');
    act(() => screen.getByTestId('menu').click());
    expect(screen.getByTestId('pill').textContent).toBe(start);
  });
});
