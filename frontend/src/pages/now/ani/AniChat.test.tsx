// @vitest-environment jsdom
import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { AniChat } from './AniChat';
import * as chat from './chat';
import type { Turn } from './chat';

vi.mock('./chat', async (orig) => ({
  ...(await orig<typeof import('./chat')>()),
  loadTurns: vi.fn(),
  sendMessage: vi.fn(),
  readTurn: vi.fn(),
  wakeAgent: vi.fn(),
  deleteTurn: vi.fn(),
  clearChat: vi.fn(),
}));

const turn = (over: Partial<Turn>): Turn => ({
  id: 't1',
  status: 'queued',
  text: 'Как мой день?',
  reply: null,
  error: null,
  tools: [],
  created_at: '2026-10-25T16:00:00Z',
  updated_at: '2026-10-25T16:00:00Z',
  ...over,
});

beforeEach(() => {
  vi.useFakeTimers({ shouldAdvanceTime: true });
});
afterEach(() => {
  cleanup();
  vi.useRealTimers();
  vi.clearAllMocks();
});

describe('AniChat', () => {
  it('wakes her agent and shows the chat so far', async () => {
    vi.mocked(chat.loadTurns).mockResolvedValue([turn({ status: 'done', reply: 'Спокойный.' })]);
    render(<AniChat tz="Asia/Almaty" onClose={() => {}} />);
    expect(chat.wakeAgent).toHaveBeenCalledOnce();
    expect(await screen.findByText('Спокойный.')).toBeTruthy();
  });

  it('sends on Enter and polls the turn until her reply', async () => {
    vi.mocked(chat.loadTurns).mockResolvedValue([]);
    vi.mocked(chat.sendMessage).mockResolvedValue(turn({}));
    vi.mocked(chat.readTurn)
      .mockResolvedValueOnce(turn({ status: 'running' }))
      .mockResolvedValueOnce(turn({ status: 'done', reply: 'Три задачи и тренировка.' }));
    const onReply = vi.fn();
    render(<AniChat tz="Asia/Almaty" onClose={() => {}} onReply={onReply} />);
    await screen.findByText(/Write to her/);

    const box = screen.getByLabelText('Message to Ani');
    fireEvent.change(box, { target: { value: 'Как мой день?' } });
    fireEvent.keyDown(box, { key: 'Enter' });
    expect(await screen.findByText('Waking up')).toBeTruthy();
    expect(chat.sendMessage).toHaveBeenCalledWith('Как мой день?', 'Asia/Almaty');

    await act(async () => {
      await vi.advanceTimersByTimeAsync(chat.POLL_MS);
    });
    expect(await screen.findByText('Thinking')).toBeTruthy();
    await act(async () => {
      await vi.advanceTimersByTimeAsync(chat.POLL_MS);
    });
    expect(await screen.findByText('Три задачи и тренировка.')).toBeTruthy();
    expect(chat.readTurn).toHaveBeenCalledTimes(2);
    expect(onReply).toHaveBeenCalledOnce();
    expect(onReply.mock.calls[0][0].reply).toBe('Три задачи и тренировка.');
    await act(async () => {
      await vi.advanceTimersByTimeAsync(chat.POLL_MS * 3);
    });
    expect(chat.readTurn).toHaveBeenCalledTimes(2);
  });

  it('says why a message was not taken', async () => {
    vi.mocked(chat.loadTurns).mockResolvedValue([]);
    vi.mocked(chat.sendMessage).mockRejectedValue(new Error('offline'));
    render(<AniChat tz="UTC" onClose={() => {}} />);
    await screen.findByText(/Write to her/);
    fireEvent.change(screen.getByLabelText('Message to Ani'), { target: { value: 'Hi' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send' }));
    expect((await screen.findByRole('alert')).textContent).toMatch(/not sent/);
  });

  it('closes on Escape', async () => {
    vi.mocked(chat.loadTurns).mockResolvedValue([]);
    const onClose = vi.fn();
    render(<AniChat tz="UTC" onClose={onClose} />);
    fireEvent.keyDown(document, { key: 'Escape' });
    expect(onClose).toHaveBeenCalledOnce();
  });

  it('does not speak with the sound off, and remembers it', async () => {
    localStorage.removeItem('now:ani:chat:sound');
    vi.mocked(chat.loadTurns).mockResolvedValue([]);
    vi.mocked(chat.sendMessage).mockResolvedValue(turn({}));
    vi.mocked(chat.readTurn).mockResolvedValue(turn({ status: 'done', reply: 'Ок.' }));
    const onReply = vi.fn();
    render(<AniChat tz="UTC" onClose={() => {}} onReply={onReply} />);
    await screen.findByText(/Write to her/);
    fireEvent.click(screen.getByRole('button', { name: /mute/ }));
    expect(localStorage.getItem('now:ani:chat:sound')).toBe('off');

    fireEvent.change(screen.getByLabelText('Message to Ani'), { target: { value: 'Hi' } });
    fireEvent.keyDown(screen.getByLabelText('Message to Ani'), { key: 'Enter' });
    await screen.findByText('Waking up');
    await act(async () => {
      await vi.advanceTimersByTimeAsync(chat.POLL_MS);
    });
    expect(await screen.findByText('Ок.')).toBeTruthy();
    expect(onReply).not.toHaveBeenCalled();
    localStorage.removeItem('now:ani:chat:sound');
  });

  it('deletes one message and clears the chat after a confirm', async () => {
    vi.mocked(chat.loadTurns).mockResolvedValue([
      turn({ id: 'a', status: 'done', text: 'Первое', reply: 'Раз.' }),
      turn({ id: 'b', status: 'done', text: 'Второе', reply: 'Два.' }),
    ]);
    vi.mocked(chat.deleteTurn).mockResolvedValue();
    vi.mocked(chat.clearChat).mockResolvedValue();
    render(<AniChat tz="UTC" onClose={() => {}} />);
    await screen.findByText('Раз.');

    fireEvent.click(screen.getAllByRole('button', { name: 'Delete this message' })[0]);
    await act(async () => {});
    expect(chat.deleteTurn).toHaveBeenCalledWith('a');
    expect(screen.queryByText('Раз.')).toBeNull();
    expect(screen.getByText('Два.')).toBeTruthy();

    fireEvent.click(screen.getByRole('button', { name: 'Clear the chat' }));
    expect(chat.clearChat).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole('button', { name: 'Clear' }));
    await act(async () => {});
    expect(chat.clearChat).toHaveBeenCalledOnce();
    expect(await screen.findByText(/Write to her/)).toBeTruthy();
  });
});
