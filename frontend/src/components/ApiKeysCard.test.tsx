// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { api } from '@/lib/api/client';

import { ApiKeysCard } from './ApiKeysCard';

/**
 * The API keys card on /account: lists full and read keys (Ani's MCP keys stay
 * in her card), shows a new key once, and revokes only after a second press.
 */

const SECRET = 'cpk_' + 'f'.repeat(64);
let posted: unknown[] = [];

beforeEach(() => {
  posted = [];
  vi.spyOn(api, 'getJson').mockImplementation(async (path: string) => {
    if (path !== '/api/v1/account/api-keys') throw new Error('unexpected ' + path);
    return {
      data: [
        {
          id: 'k1',
          name: 'Ani MCP',
          prefix: 'cpk_ab12',
          scope: 'ani',
          last_used_at: null,
          revoked_at: null,
          created_at: '2026-10-28T10:00:00Z',
        },
        {
          id: 'k2',
          name: 'cli',
          prefix: 'cpk_cd34',
          scope: 'full',
          last_used_at: null,
          revoked_at: null,
          created_at: '2026-09-30T10:00:00Z',
        },
        {
          id: 'k3',
          name: 'old',
          prefix: 'cpk_ef56',
          scope: 'full',
          last_used_at: null,
          revoked_at: '2026-10-01T10:00:00Z',
          created_at: '2026-09-01T10:00:00Z',
        },
      ],
      total: 3,
    };
  });
  vi.spyOn(api, 'postJson').mockImplementation(async (_p: string, opts?: { body?: unknown }) => {
    posted.push(opts?.body);
    return {
      id: 'k4',
      name: 'n8n',
      prefix: 'cpk_ffff',
      scope: 'read',
      last_used_at: null,
      revoked_at: null,
      created_at: '2026-10-29T10:00:00Z',
      key: SECRET,
    };
  });
  vi.spyOn(api, 'deleteJson').mockResolvedValue(undefined);
});

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

function renderCard() {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  return render(
    <QueryClientProvider client={qc}>
      <ApiKeysCard />
    </QueryClientProvider>,
  );
}

describe('ApiKeysCard', () => {
  it('lists live full and read keys only', async () => {
    renderCard();
    expect(await screen.findByText('cli')).toBeTruthy();
    expect(screen.queryByText('Ani MCP')).toBeNull();
    expect(screen.queryByText('old')).toBeNull();
  });

  it('creates a key with the chosen scope and shows it once', async () => {
    renderCard();
    await screen.findByText('cli');
    fireEvent.change(screen.getByLabelText('Key name'), { target: { value: ' n8n ' } });
    fireEvent.change(screen.getByLabelText('Access'), { target: { value: 'read' } });
    fireEvent.click(screen.getByRole('button', { name: 'Create key' }));
    expect(await screen.findByText(SECRET)).toBeTruthy();
    expect(posted).toEqual([{ name: 'n8n', scope: 'read' }]);
    fireEvent.click(screen.getByRole('button', { name: 'Done' }));
    expect(screen.queryByText(SECRET)).toBeNull();
  });

  it('revokes only after the second press', async () => {
    renderCard();
    fireEvent.click(await screen.findByText('cli'));
    fireEvent.click(screen.getByRole('button', { name: 'Revoke…' }));
    expect(api.deleteJson).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole('button', { name: 'Revoke' }));
    await waitFor(() => expect(api.deleteJson).toHaveBeenCalledWith('/api/v1/account/api-keys/k2'));
  });
});
