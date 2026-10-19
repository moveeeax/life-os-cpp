// @vitest-environment jsdom
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { MemoryRouter } from 'react-router';
import { afterEach, describe, expect, it } from 'vitest';

import { Tabs } from './Tabs';

const items = [
  'Ledger',
  'Summary',
  'Reports',
  'Accounts',
  'Categories',
  'Currencies',
  'Advisor',
].map((label) => ({ label, to: `/money/${label.toLowerCase()}` }));

function mount() {
  render(
    <MemoryRouter initialEntries={['/money/ledger']}>
      <Tabs items={items} max={5} label="Money" />
    </MemoryRouter>,
  );
  return screen.getByRole('button', { name: /More/ });
}

afterEach(cleanup);

describe('Tabs More menu with the keyboard', () => {
  it('ArrowDown on the button opens the menu on its first item', () => {
    const button = mount();
    fireEvent.keyDown(button, { key: 'ArrowDown' });
    const menu = screen.getByRole('menu');
    const first = screen.getAllByRole('menuitem')[0];
    expect(menu).toBeTruthy();
    // Four tabs stay in the row, the fifth slot is More: Categories leads it.
    expect(first.textContent).toBe('Categories');
    expect(document.activeElement).toBe(first);
  });

  it('ArrowUp wraps to the last item and Escape returns to the button', () => {
    const button = mount();
    fireEvent.keyDown(button, { key: 'ArrowDown' });
    const all = screen.getAllByRole('menuitem');
    const first = all[0];
    const last = all[all.length - 1];
    fireEvent.keyDown(first, { key: 'ArrowUp' });
    expect(document.activeElement).toBe(last);
    fireEvent.keyDown(last, { key: 'ArrowDown' });
    expect(document.activeElement).toBe(first);
    fireEvent.keyDown(first, { key: 'Escape' });
    expect(screen.queryByRole('menu')).toBeNull();
    expect(document.activeElement).toBe(button);
  });

  it('closes when the focus moves outside, and points aria-controls at the open menu only', () => {
    render(
      <MemoryRouter initialEntries={['/money/ledger']}>
        <Tabs items={items} max={5} label="Money" />
        <button type="button">Elsewhere</button>
      </MemoryRouter>,
    );
    const button = screen.getByRole('button', { name: /More/ });
    expect(button.getAttribute('aria-controls')).toBeNull();
    fireEvent.keyDown(button, { key: 'ArrowDown' });
    const menu = screen.getByRole('menu');
    expect(button.getAttribute('aria-controls')).toBe(menu.id);
    const first = screen.getAllByRole('menuitem')[0];
    const elsewhere = screen.getByRole('button', { name: 'Elsewhere' });
    fireEvent.blur(first, { relatedTarget: elsewhere });
    expect(screen.queryByRole('menu')).toBeNull();
    expect(button.getAttribute('aria-controls')).toBeNull();
  });
});
