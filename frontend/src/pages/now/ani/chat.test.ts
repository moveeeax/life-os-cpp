import { describe, expect, it, vi } from 'vitest';

import { ApiClientError, api } from '@/lib/api/client';

import {
  actionState,
  chatOf,
  askAgent,
  confirmAction,
  confirmWords,
  failure,
  isDoing,
  isOpen,
  loadTurns,
  refusal,
  sendMessage,
  type Action,
  type Turn,
} from './chat';

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
  actions: [],
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

  it('words what became of a proposal; a pending one has buttons instead', () => {
    const a = (status: Action['status']): Action => ({
      id: 'x',
      kind: 'add_note',
      summary: 'Note',
      status,
      error: null,
    });
    expect(actionState(a('pending'))).toBeNull();
    expect(actionState(a('running'))).toBe('Doing it');
    expect(actionState(a('done'))).toBe('Done');
    expect(actionState(a('expired'))).toBe('Not confirmed in time');
    expect(actionState(a('failed'))).toBe('Did not work');
    expect(isDoing({ ...turn('a', 'done'), actions: [a('confirmed')] })).toBe(true);
    expect(isDoing({ ...turn('a', 'done'), actions: [a('done'), a('pending')] })).toBe(false);
  });

  it('confirms a proposal by its id', async () => {
    vi.mocked(api.postJson).mockResolvedValueOnce({ data: { id: 'x', status: 'confirmed' } });
    expect((await confirmAction('x')).status).toBe('confirmed');
    expect(api.postJson).toHaveBeenLastCalledWith('/api/v1/ani/actions/x/confirm');
  });

  it('sends the own text of a Telegram confirm', async () => {
    vi.mocked(api.postJson).mockResolvedValueOnce({ data: { id: 'x', status: 'confirmed' } });
    await confirmAction('x', 'Буду в восемь');
    expect(api.postJson).toHaveBeenLastCalledWith('/api/v1/ani/actions/x/confirm', {
      body: { text: 'Буду в восемь' },
    });
  });

  it('asks her agent from the voice and waits for the reply', async () => {
    vi.mocked(api.postJson).mockResolvedValueOnce({ data: turn('q', 'queued') });
    vi.mocked(api.getJson)
      .mockResolvedValueOnce({ data: turn('q', 'running') })
      .mockResolvedValueOnce({ data: { ...turn('q', 'done'), reply: 'Добавить молоко?' } });
    const wait = vi.fn(() => Promise.resolve());
    const t = await askAgent('Добавь молоко', 'UTC', wait);
    expect(t.reply).toBe('Добавить молоко?');
    expect(wait).toHaveBeenCalledTimes(2);
    expect(api.getJson).toHaveBeenLastCalledWith('/api/v1/ani/turns/q');
  });
});

describe('chatOf', () => {
  it('reads the chat from a Telegram proposal', () => {
    expect(chatOf('Send to Анна: «Ок»')).toBe('Анна');
    expect(chatOf('Edit your message in Work chat to «x»')).toBe('Work chat');
    expect(chatOf('Add the task «Паспорт»')).toBeNull();
  });
});

describe('confirmWords', () => {
  it('shows the whole message of a Telegram proposal, not the cut summary', () => {
    const long = 'Конечно, увидимся в пять. '.repeat(20) + 'А вот мои заметки.';
    const cut = Array.from(long).slice(0, 300).join('') + '…';
    expect(confirmWords({ summary: `Send to Bob: «${cut}»`, text: long })).toBe(
      `Send to Bob: «${long}»`,
    );
    expect(confirmWords({ summary: 'Edit your message in Work to «x»', text: 'x' })).toBe(
      'Edit your message in Work to «x»',
    );
    expect(confirmWords({ summary: 'Add the task «Паспорт»', text: null })).toBe(
      'Add the task «Паспорт»',
    );
    expect(confirmWords({ summary: 'Send to Bob: «старый»', text: 'новый' })).toBe(
      'Send to Bob: «старый» новый',
    );
  });
});
