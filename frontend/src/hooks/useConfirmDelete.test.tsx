// @vitest-environment jsdom
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { useConfirmDelete } from './useConfirmDelete';

afterEach(cleanup);

type RowT = { id: string; name: string };

function List({
  mutate,
  closeOnSuccess,
}: {
  mutate: (id: string, o?: { onSuccess?: () => void }) => void;
  closeOnSuccess?: boolean;
}) {
  const deleting = useConfirmDelete<RowT>();
  return (
    <>
      <button type="button" onClick={() => deleting.ask({ id: 'r1', name: 'Iron' })}>
        Ask
      </button>
      {deleting.dialog(
        { mutate, isPending: false },
        (d) => ({ title: `Delete ${d.name}?`, description: 'Gone for good.' }),
        { closeOnSuccess },
      )}
    </>
  );
}

describe('useConfirmDelete', () => {
  it('renders nothing until a row is asked, then deletes it by id', () => {
    const mutate = vi.fn();
    render(<List mutate={mutate} />);
    expect(screen.queryByText('Delete Iron?')).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: 'Ask' }));
    expect(screen.getByText('Delete Iron?')).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: 'Delete' }));
    expect(mutate).toHaveBeenCalledWith('r1');
  });

  it('Cancel closes it; closeOnSuccess closes it from the mutate call', () => {
    const mutate = vi.fn((_id: string, o?: { onSuccess?: () => void }) => o?.onSuccess?.());
    render(<List mutate={mutate} closeOnSuccess />);
    fireEvent.click(screen.getByRole('button', { name: 'Ask' }));
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(screen.queryByText('Delete Iron?')).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: 'Ask' }));
    fireEvent.click(screen.getByRole('button', { name: 'Delete' }));
    expect(mutate).toHaveBeenCalledWith(
      'r1',
      expect.objectContaining({ onSuccess: expect.any(Function) }),
    );
    expect(screen.queryByText('Delete Iron?')).toBeNull();
  });
});
