// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { api } from '@/lib/api/client';

import { AniSettingsCard } from './AniSettingsCard';

/**
 * Ani's card on /account: the bare switch asks the 18+ question before it
 * sends anything; the webhook URL copies and is replaced only after a confirm.
 */

const URL1 = 'https://life.test/api/v1/ani/hook/aniwh_' + 'a'.repeat(48);
const URL2 = 'https://life.test/api/v1/ani/hook/aniwh_' + 'b'.repeat(48);

let bare = false;

beforeEach(() => {
  bare = false;
  vi.spyOn(api, 'getJson').mockImplementation(async (path: string) => {
    if (path === '/api/v1/ani/prefs') return { data: { bare } };
    if (path === '/api/v1/ani/webhook')
      return {
        data: {
          url: URL1,
          recent: [
            { id: 'i1', kind: 'say', status: 'delivered', created_at: '2026-10-27T10:00:00Z' },
          ],
        },
      };
    throw new Error('unexpected ' + path);
  });
  vi.spyOn(api, 'putJson').mockImplementation(async (_p: string, opts?: { body?: unknown }) => {
    bare = (opts?.body as { bare: boolean }).bare;
    return { data: { bare } };
  });
  vi.spyOn(api, 'postJson').mockResolvedValue({ data: { url: URL2, recent: [] } });
});

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

function renderCard() {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  return render(
    <QueryClientProvider client={qc}>
      <AniSettingsCard />
    </QueryClientProvider>,
  );
}

describe('AniSettingsCard', () => {
  it('sends bare on with adult only after the 18+ confirm', async () => {
    renderCard();
    const sw = await screen.findByRole('switch', { name: /bare outfit/i });
    await waitFor(() => expect((sw as HTMLButtonElement).disabled).toBe(false));
    fireEvent.click(sw);
    expect(api.putJson).not.toHaveBeenCalled();
    expect(screen.getByText(/only for adults/i)).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: /i am 18/i }));
    await waitFor(() =>
      expect(api.putJson).toHaveBeenCalledWith('/api/v1/ani/prefs', {
        body: { bare: true, adult: true },
      }),
    );
  });

  it('turns bare off without a question', async () => {
    bare = true;
    renderCard();
    const sw = await screen.findByRole('switch', { name: /bare outfit/i });
    await waitFor(() => expect(sw.getAttribute('aria-checked')).toBe('true'));
    fireEvent.click(sw);
    await waitFor(() =>
      expect(api.putJson).toHaveBeenCalledWith('/api/v1/ani/prefs', { body: { bare: false } }),
    );
  });

  it('copies the webhook url', async () => {
    const writeText = vi.fn().mockResolvedValue(undefined);
    Object.assign(navigator, { clipboard: { writeText } });
    renderCard();
    const field = (await screen.findByLabelText(/webhook url/i)) as HTMLInputElement;
    expect(field.value).toBe(URL1);
    expect(field.classList.contains('num')).toBe(true); // a secret: Hide numbers blurs it
    fireEvent.click(screen.getByRole('button', { name: /^copy$/i }));
    await waitFor(() => expect(writeText).toHaveBeenCalledWith(URL1));
    expect(await screen.findByRole('button', { name: /copied/i })).toBeTruthy();
  });

  it('replaces the url only after the confirm', async () => {
    renderCard();
    await screen.findByLabelText(/webhook url/i);
    fireEvent.click(screen.getByRole('button', { name: /replace url/i }));
    expect(api.postJson).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole('button', { name: /replace it/i }));
    await waitFor(() => expect(api.postJson).toHaveBeenCalledWith('/api/v1/ani/webhook/rotate'));
  });

  it('lists recent calls without their text', async () => {
    renderCard();
    const row = (await screen.findByText(/delivered/)).closest('li')!;
    expect(row.textContent).toMatch(/^say, delivered/);
    expect(row.textContent).not.toMatch(/Сборка/);
  });
});
