// @vitest-environment jsdom
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { MoveDate } from './TaskRow';

/**
 * A date input reports a value after one keystroke into a segment, so "Move
 * date" must not save on change: typing "25" into the day would save the 2nd,
 * and a half-typed year (0002) is refused by the API.
 */
afterEach(cleanup);

function mount(due: string | null = '2026-10-18') {
  const onSave = vi.fn();
  const onCancel = vi.fn();
  render(<MoveDate due={due} onSave={onSave} onCancel={onCancel} />);
  const input = screen.getByLabelText('New due date') as HTMLInputElement;
  return { onSave, onCancel, input };
}

describe('Move date', () => {
  it('does not save while the date is typed', () => {
    const { onSave, input } = mount();
    fireEvent.change(input, { target: { value: '2026-10-02' } });
    fireEvent.change(input, { target: { value: '2026-10-25' } });
    expect(onSave).not.toHaveBeenCalled();
  });

  it('saves on Save, and on Enter through the form', () => {
    const { onSave, input } = mount();
    fireEvent.change(input, { target: { value: '2026-10-25' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(onSave).toHaveBeenCalledWith('2026-10-25');
    fireEvent.submit(input.form!);
    expect(onSave).toHaveBeenCalledTimes(2);
  });

  it('refuses a year the API does not take', () => {
    const { onSave, input } = mount();
    fireEvent.change(input, { target: { value: '0002-10-25' } });
    const save = screen.getByRole('button', { name: 'Save' }) as HTMLButtonElement;
    expect(save.disabled).toBe(true);
    fireEvent.submit(input.form!);
    expect(onSave).not.toHaveBeenCalled();
  });

  it('clears the date with No date and closes on Escape', () => {
    const { onSave, onCancel, input } = mount();
    fireEvent.click(screen.getByRole('button', { name: 'No date' }));
    expect(onSave).toHaveBeenCalledWith(null);
    fireEvent.keyDown(input, { key: 'Escape' });
    expect(onCancel).toHaveBeenCalled();
  });
});
