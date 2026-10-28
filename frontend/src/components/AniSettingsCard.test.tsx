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
let keys: { id: string; name: string; prefix: string; scope: string; created_at: string }[] = [];
let reminders: {
  id: string;
  text: string;
  time: string;
  days: number;
  tz: string;
  next_at: string;
}[] = [];

beforeEach(() => {
  bare = false;
  reminders = [
    {
      id: 'r1',
      text: 'Выпей воды',
      time: '10:00',
      days: 31,
      tz: 'Asia/Almaty',
      next_at: '2026-10-29T05:00:00Z',
    },
  ];
  keys = [
    {
      id: 'k1',
      name: 'Ani MCP',
      prefix: 'cpk_ab12',
      scope: 'ani',
      created_at: '2026-10-27T10:00:00Z',
    },
    {
      id: 'k2',
      name: 'n8n',
      prefix: 'cpk_cd34',
      scope: 'full',
      created_at: '2026-10-20T10:00:00Z',
    },
  ];
  vi.spyOn(api, 'getJson').mockImplementation(async (path: string) => {
    if (path === '/api/v1/ani/prefs') return { data: { bare } };
    if (path === '/api/v1/ani/webhook')
      return {
        data: {
          url: URL1,
          recent: [
            {
              id: 'i1',
              kind: 'ask',
              status: 'delivered',
              via: 'mcp',
              say: 'Чай?',
              answer: 'Да',
              created_at: '2026-10-27T10:00:00Z',
            },
          ],
        },
      };
    if (path === '/api/v1/account/api-keys') return { data: keys, total: keys.length };
    if (path === '/api/v1/ani/reminders') return { data: reminders };
    throw new Error('unexpected ' + path);
  });
  vi.spyOn(api, 'putJson').mockImplementation(async (_p: string, opts?: { body?: unknown }) => {
    bare = (opts?.body as { bare: boolean }).bare;
    return { data: { bare } };
  });
  vi.spyOn(api, 'postJson').mockImplementation(async (path: string) => {
    if (path === '/api/v1/ani/reminders') return { data: { id: 'r2' } };
    if (path === '/api/v1/account/api-keys')
      return {
        id: 'k3',
        name: 'Ani MCP',
        prefix: 'cpk_ef56',
        scope: 'ani',
        created_at: '2026-10-28T10:00:00Z',
        key: 'cpk_' + 'f'.repeat(64),
      };
    return { data: { url: URL2, recent: [] } };
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

  it('lists the log as calm rows, the text only once a row is opened', async () => {
    renderCard();
    expect(await screen.findByRole('button', { name: /ask/ })).toBeTruthy();
    expect(screen.queryByText('Чай?')).toBeNull();
  });

  it('shows the MCP endpoint and lists only the ani keys', async () => {
    renderCard();
    expect(await screen.findByText(/\/api\/v1\/ani\/mcp/)).toBeTruthy();
    expect(await screen.findByText('cpk_ab12')).toBeTruthy();
    expect(screen.queryByText('cpk_cd34')).toBeNull();
  });

  it('makes an ani key and shows it once with the command', async () => {
    renderCard();
    fireEvent.click(await screen.findByRole('button', { name: /create key/i }));
    await waitFor(() =>
      expect(api.postJson).toHaveBeenCalledWith('/api/v1/account/api-keys', {
        body: { name: 'Ani MCP', scope: 'ani' },
      }),
    );
    const shown = await screen.findByText(/claude mcp add --transport http ani/);
    expect(shown.textContent).toContain('Authorization: Bearer cpk_' + 'f'.repeat(64));
    expect(shown.closest('.num')).toBeTruthy();
  });

  it('revokes an ani key', async () => {
    renderCard();
    fireEvent.click(await screen.findByRole('button', { name: /revoke cpk_ab12/i }));
    await waitFor(() => expect(api.deleteJson).toHaveBeenCalledWith('/api/v1/account/api-keys/k1'));
  });

  it('lists reminders, adds one with weekdays and removes one', async () => {
    renderCard();
    expect(await screen.findByText('Выпей воды')).toBeTruthy();
    expect(screen.getByText(/10:00 · Mon–Fri/)).toBeTruthy();
    fireEvent.change(screen.getByLabelText('Reminder text'), { target: { value: 'Разминка' } });
    fireEvent.change(screen.getByLabelText('Reminder time'), { target: { value: '18:30' } });
    fireEvent.click(screen.getByRole('button', { name: 'Sat' }));
    fireEvent.click(screen.getByRole('button', { name: 'Sun' }));
    fireEvent.click(screen.getByRole('button', { name: 'Add reminder' }));
    await waitFor(() =>
      expect(api.postJson).toHaveBeenCalledWith('/api/v1/ani/reminders', {
        body: { text: 'Разминка', time: '18:30', days: [1, 2, 3, 4, 5], tz: expect.any(String) },
      }),
    );
    fireEvent.click(screen.getByRole('button', { name: 'Remove Выпей воды' }));
    await waitFor(() => expect(api.deleteJson).toHaveBeenCalledWith('/api/v1/ani/reminders/r1'));
  });

  it('opens a log row for who sent it, her line and the answer', async () => {
    renderCard();
    fireEvent.click(await screen.findByRole('button', { name: /ask/ }));
    expect(screen.getByText('MCP, delivered')).toBeTruthy();
    expect(screen.getByText('Чай?')).toBeTruthy();
    expect(screen.getByText('Answer: Да')).toBeTruthy();
  });
});
