import { describe, expect, it, vi } from 'vitest';

import { ApiClientError, api } from '@/lib/api/client';

import { failure, isOpen, loadTurns, refusal, sendMessage, type Turn } from './chat';

vi.mock('@/lib/api/client', async (orig) => ({
  ...(await orig<typeof import('@/lib/api/client')>()),
  api: { getJson: vi.fn(), postJson: vi.fn(), POST: vi.fn() },
}));

const turn = (id: string, status: Turn['status']): Turn => ({
  id,
  status,
  text: 'Привет',
  reply: null,
  error: null,
  tools: [],
  created_at: '2026-10-25T16:00:00Z',
  updated_at: '2026-10-25T16:00:00Z',
});

describe('chat', () => {
  it('shows the chat oldest first', async () => {
    vi.mocked(api.getJson).mockResolvedValueOnce({ data: [turn('b', 'done'), turn('a', 'done')] });
    expect((await loadTurns()).map((t) => t.id)).toEqual(['a', 'b']);
  });

  it('sends the text with the zone', async () => {
    vi.mocked(api.postJson).mockResolvedValueOnce({ data: turn('c', 'queued') });
    await sendMessage('Как мой день?', 'Asia/Almaty');
    expect(api.postJson).toHaveBeenCalledWith('/api/v1/ani/messages', {
      body: { text: 'Как мой день?', tz: 'Asia/Almaty' },
    });
  });

  it('a queued or running turn is open', () => {
    expect(isOpen(turn('a', 'queued'))).toBe(true);
    expect(isOpen(turn('a', 'running'))).toBe(true);
    expect(isOpen(turn('a', 'done'))).toBe(false);
    expect(isOpen(turn('a', 'failed'))).toBe(false);
  });

  it('words every failure and refusal', () => {
    expect(failure('budget')).toMatch(/limit/);
    expect(failure('busy')).toMatch(/taken/);
    expect(failure(null)).toMatch(/went wrong/);
    const err = (status: number, code?: string) =>
      new ApiClientError({ status, code, message: code ?? 'x' });
    expect(refusal(err(409, 'turn_open'))).toMatch(/still answering/);
    expect(refusal(err(429))).toMatch(/one hour/);
    expect(refusal(err(422, 'budget'))).toBe(failure('budget'));
    expect(refusal(new Error('offline'))).toMatch(/not sent/);
  });
});
