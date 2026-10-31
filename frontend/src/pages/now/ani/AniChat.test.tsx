// @vitest-environment jsdom
import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { AniChat, DIARY_ASK, QUIZ_ASK } from './AniChat';
import * as chat from './chat';
import type { Turn } from './chat';

vi.mock('./chat', async (orig) => ({
  ...(await orig<typeof import('./chat')>()),
  loadTurns: vi.fn(),
  sendMessage: vi.fn(),
  readTurn: vi.fn(),
  wakeAgent: vi.fn(),
  deleteTurn: vi.fn(),
  confirmAction: vi.fn(),
  cancelAction: vi.fn(),
  clearChat: vi.fn(),
}));

const turn = (over: Partial<Turn>): Turn => ({
  id: 't1',
  status: 'queued',
  text: 'Как мой день?',
  reply: null,
  error: null,
  tools: [],
  actions: [],
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
  it('marks webhook turns and shows a silent answer as silence', async () => {
    vi.mocked(chat.loadTurns).mockResolvedValue([
      turn({
        id: 'h1',
        origin: 'hook',
        status: 'done',
        text: '[telegram] Реклама',
        reply: 'SILENT',
      }),
      turn({ id: 'h3', origin: 'hook', status: 'done', text: '[ci] ok', reply: 'Silent.' }),
      turn({
        id: 'h2',
        origin: 'hook',
        status: 'done',
        text: '[telegram] Маша: созвон?',
        reply: 'Маша зовёт.',
      }),
    ]);
    render(<AniChat tz="Asia/Almaty" onClose={() => {}} />);
    expect(await screen.findByText('Маша зовёт.')).toBeTruthy();
    expect(screen.getAllByText(/from your webhook/i)).toHaveLength(3);
    expect(screen.queryByText('SILENT')).toBeNull();
    expect(screen.queryByText('Silent.')).toBeNull();
    expect(screen.getAllByText(/kept quiet/i)).toHaveLength(2);
  });

  it('takes a message while a webhook turn is open, and does not speak its reply', async () => {
    vi.mocked(chat.loadTurns).mockResolvedValue([
      turn({ id: 'h1', origin: 'hook', status: 'running', text: '[telegram] Маша' }),
    ]);
    vi.mocked(chat.readTurn).mockResolvedValue(
      turn({
        id: 'h1',
        origin: 'hook',
        status: 'done',
        text: '[telegram] Маша',
        reply: 'Маша пишет.',
      }),
    );
    const onReply = vi.fn();
    render(<AniChat tz="Asia/Almaty" onClose={() => {}} onReply={onReply} />);
    const box = await screen.findByLabelText('Message to Ani');
    await screen.findByText('[telegram] Маша');
    fireEvent.change(box, { target: { value: 'Привет' } });
    expect((screen.getByRole('button', { name: /send/i }) as HTMLButtonElement).disabled).toBe(
      false,
    );
    await act(async () => {
      await vi.advanceTimersByTimeAsync(chat.POLL_MS);
    });
    expect(await screen.findByText('Маша пишет.')).toBeTruthy();
    expect(onReply).not.toHaveBeenCalled();
  });

  it('marks MCP turns, takes a message beside one, and does not speak its reply', async () => {
    vi.mocked(chat.loadTurns).mockResolvedValue([
      turn({ id: 'm1', origin: 'mcp', status: 'running', text: 'Как дела?' }),
    ]);
    vi.mocked(chat.readTurn).mockResolvedValue(
      turn({ id: 'm1', origin: 'mcp', status: 'done', text: 'Как дела?', reply: 'Хорошо.' }),
    );
    const onReply = vi.fn();
    render(<AniChat tz="Asia/Almaty" onClose={() => {}} onReply={onReply} />);
    const box = await screen.findByLabelText('Message to Ani');
    expect(await screen.findByText(/from mcp/i)).toBeTruthy();
    fireEvent.change(box, { target: { value: 'Привет' } });
    expect((screen.getByRole('button', { name: /send/i }) as HTMLButtonElement).disabled).toBe(
      false,
    );
    await act(async () => {
      await vi.advanceTimersByTimeAsync(chat.POLL_MS);
    });
    expect(await screen.findByText('Хорошо.')).toBeTruthy();
    expect(onReply).not.toHaveBeenCalled();
  });

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

  it('shows a proposal with Confirm and reads the turn until it is made', async () => {
    const pending = {
      id: 'a1',
      kind: 'add_note' as const,
      summary: 'Note to the inbox: «молоко»',
      status: 'pending' as const,
      error: null,
    };
    vi.mocked(chat.loadTurns).mockResolvedValue([
      turn({ status: 'done', reply: 'Записать?', actions: [pending] }),
    ]);
    vi.mocked(chat.confirmAction).mockResolvedValue({ ...pending, status: 'confirmed' });
    vi.mocked(chat.readTurn).mockResolvedValue(
      turn({ status: 'done', reply: 'Записать?', actions: [{ ...pending, status: 'done' }] }),
    );
    render(<AniChat tz="UTC" onClose={() => {}} />);
    expect(await screen.findByText('Note to the inbox: «молоко»')).toBeTruthy();
    fireEvent.click(screen.getByText('Confirm'));
    expect(await screen.findByText('Doing it')).toBeTruthy();
    expect(chat.confirmAction).toHaveBeenCalledWith('a1');
    await act(async () => {
      await vi.advanceTimersByTimeAsync(chat.POLL_MS);
    });
    expect(await screen.findByText('Done')).toBeTruthy();
    expect(screen.queryByText('Confirm')).toBeNull();
  });

  it('lets the person reword a Telegram message before Confirm', async () => {
    const pending = {
      id: 'a3',
      kind: 'tg_send' as const,
      summary: 'Send to Anna: «Буду в семь»',
      text: 'Буду в семь',
      status: 'pending' as const,
      error: null,
    };
    vi.mocked(chat.loadTurns).mockResolvedValue([
      turn({ status: 'done', reply: 'Ответить Анне?', actions: [pending] }),
    ]);
    vi.mocked(chat.confirmAction).mockResolvedValue({ ...pending, status: 'confirmed' });
    render(<AniChat tz="UTC" onClose={() => {}} />);
    fireEvent.click(await screen.findByText('Edit'));
    const box = screen.getByLabelText('The message');
    expect((box as HTMLTextAreaElement).value).toBe('Буду в семь');
    fireEvent.change(box, { target: { value: 'Буду в восемь' } });
    fireEvent.click(screen.getByText('Confirm'));
    expect(await screen.findByText('Doing it')).toBeTruthy();
    expect(chat.confirmAction).toHaveBeenCalledWith('a3', 'Буду в восемь');
  });

  it('offers no Edit for other proposals', async () => {
    vi.mocked(chat.loadTurns).mockResolvedValue([
      turn({
        status: 'done',
        reply: 'Записать?',
        actions: [
          {
            id: 'a4',
            kind: 'add_note',
            summary: 'Note to the inbox: «x»',
            text: null,
            status: 'pending',
            error: null,
          },
        ],
      }),
    ]);
    render(<AniChat tz="UTC" onClose={() => {}} />);
    expect(await screen.findByText('Note to the inbox: «x»')).toBeTruthy();
    expect(screen.queryByText('Edit')).toBeNull();
  });

  it('cancels a proposal', async () => {
    const pending = {
      id: 'a2',
      kind: 'create_task' as const,
      summary: 'Add the task «Паспорт»',
      status: 'pending' as const,
      error: null,
    };
    vi.mocked(chat.loadTurns).mockResolvedValue([
      turn({ status: 'done', reply: 'Добавить?', actions: [pending] }),
    ]);
    vi.mocked(chat.cancelAction).mockResolvedValue({ ...pending, status: 'cancelled' });
    render(<AniChat tz="UTC" onClose={() => {}} />);
    fireEvent.click(await screen.findByText('Cancel'));
    expect(await screen.findByText('Cancelled')).toBeTruthy();
    expect(chat.readTurn).not.toHaveBeenCalled();
  });

  it('QuizAndDiaryAsks: the two buttons send her asks', async () => {
    vi.mocked(chat.loadTurns).mockResolvedValue([]);
    vi.mocked(chat.sendMessage).mockResolvedValue(turn({ id: 'q1', text: QUIZ_ASK }));
    render(<AniChat tz="UTC" onClose={() => {}} />);
    fireEvent.click(await screen.findByText('Квиз'));
    await vi.waitFor(() => expect(chat.sendMessage).toHaveBeenCalledWith(QUIZ_ASK, 'UTC'));
  });

  it('a seed from her line is sent, or waits in the field when it ends with a colon', async () => {
    vi.mocked(chat.loadTurns).mockResolvedValue([]);
    vi.mocked(chat.sendMessage).mockResolvedValue(turn({ id: 'd1', text: DIARY_ASK }));
    render(<AniChat tz="UTC" seed={DIARY_ASK} onClose={() => {}} />);
    await vi.waitFor(() => expect(chat.sendMessage).toHaveBeenCalledWith(DIARY_ASK, 'UTC'));
    cleanup();
    vi.mocked(chat.sendMessage).mockClear();
    render(<AniChat tz="UTC" seed="После встречи «Sync» есть задачи: " onClose={() => {}} />);
    await act(async () => {
      await vi.advanceTimersByTimeAsync(50);
    });
    expect(((await screen.findByRole('textbox')) as HTMLTextAreaElement).value).toBe(
      'После встречи «Sync» есть задачи: ',
    );
    expect(chat.sendMessage).not.toHaveBeenCalled();
  });
});
