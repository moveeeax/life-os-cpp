// @vitest-environment jsdom
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { AniOffers } from './AniOffers';
import * as chat from './chat';

vi.mock('./chat', async (orig) => ({
  ...(await orig<typeof import('./chat')>()),
  readTurn: vi.fn(),
  confirmAction: vi.fn(),
  cancelAction: vi.fn(),
}));

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
});

const act = (id: string, kind: string, summary: string, text: string | null = null) => ({
  id,
  kind,
  summary,
  text,
  status: 'pending',
  error: null,
});

function turnWith(actions: ReturnType<typeof act>[]) {
  vi.mocked(chat.readTurn).mockResolvedValue({ id: 't1', actions } as never);
  vi.mocked(chat.confirmAction).mockResolvedValue({} as never);
}

describe('AniOffers', () => {
  it('BubbleChipsFromActions: a reply draft sends with one tap and the others go', async () => {
    turnWith([
      act('a', 'tg_send', 'Send to Anna: «Буду через 10 минут»', 'Буду через 10 минут'),
      act('b', 'tg_send', 'Send to Anna: «Опаздываю»', 'Опаздываю'),
    ]);
    render(<AniOffers turn="t1" onDone={() => {}} />);
    fireEvent.click(await screen.findByText('Буду через 10 минут'));
    await waitFor(() => expect(chat.confirmAction).toHaveBeenCalledWith('a'));
    await waitFor(() => expect(screen.queryByText('Опаздываю')).toBeNull());
  });

  it('own words go with Send', async () => {
    turnWith([act('a', 'tg_send', 'Send to Anna: «Ок»', 'Ок')]);
    render(<AniOffers turn="t1" onDone={() => {}} />);
    fireEvent.click(await screen.findByText('Own reply…'));
    fireEvent.change(screen.getByLabelText('The message'), { target: { value: 'Буду в семь' } });
    fireEvent.click(screen.getByText('Send'));
    await waitFor(() => expect(chat.confirmAction).toHaveBeenCalledWith('a', 'Буду в семь'));
  });

  it('AcceptAll confirms every change', async () => {
    turnWith([
      act('c1', 'create_task', 'Add the task «Паспорт»'),
      act('c2', 'calendar_add', 'Add «Focus» to the calendar, 2026-10-29 14:00–15:30'),
    ]);
    render(<AniOffers turn="t1" onDone={() => {}} />);
    fireEvent.click(await screen.findByText('Accept all (2)'));
    await waitFor(() => expect(chat.confirmAction).toHaveBeenCalledTimes(2));
  });

  it('says it is done when nothing is pending', async () => {
    turnWith([]);
    const done = vi.fn();
    render(<AniOffers turn="t1" onDone={done} />);
    await waitFor(() => expect(done).toHaveBeenCalled());
  });

  it('ThoughtBubble: her line on top, Later folds into the chat letter, a tap opens it again', async () => {
    turnWith([act('a', 'tg_send', 'Send to Анна: «Ок»', 'Ок')]);
    render(<AniOffers turn="t1" line="Анна спрашивает про Mailcow" onDone={() => {}} />);
    expect(await screen.findByText('Анна спрашивает про Mailcow')).toBeTruthy();
    fireEvent.click(screen.getByText('Later'));
    expect(screen.queryByText('Ок')).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: 'Reply to Анна' }));
    expect(await screen.findByText('Ок')).toBeTruthy();
  });

  it('own reply starts empty, and a sent reply says where it went', async () => {
    const done = vi.fn();
    turnWith([act('a', 'tg_send', 'Send to Анна: «Ок»', 'Ок')]);
    render(<AniOffers turn="t1" onDone={done} />);
    fireEvent.click(await screen.findByText('Own reply…'));
    expect((screen.getByLabelText('The message') as HTMLTextAreaElement).value).toBe('');
    fireEvent.click(screen.getByText('Back'));
    fireEvent.click(screen.getByText('Ок'));
    expect(await screen.findByText('Sent to Анна')).toBeTruthy();
    await waitFor(() => expect(done).toHaveBeenCalled(), { timeout: 3000 });
  });

  it('folds while she says another line, unless the person is writing', async () => {
    turnWith([act('a', 'tg_send', 'Send to Анна: «Ок»', 'Ок')]);
    const { rerender } = render(<AniOffers turn="t1" onDone={() => {}} hushed />);
    expect(await screen.findByRole('button', { name: 'Reply to Анна' })).toBeTruthy();
    rerender(<AniOffers turn="t1" onDone={() => {}} hushed={false} />);
    expect(await screen.findByText('Ок')).toBeTruthy();
  });

  it('a reply to one chat keeps the drafts to another', async () => {
    turnWith([
      act('a', 'tg_send', 'Send to Анна: «Да»', 'Да'),
      act('b', 'tg_send', 'Send to Анна: «Нет»', 'Нет'),
      act('c', 'tg_send', 'Send to Work: «Ок»', 'Ок'),
    ]);
    render(<AniOffers turn="t1" onDone={() => {}} />);
    fireEvent.click(await screen.findByText('Да'));
    expect(await screen.findByText('Sent to Анна')).toBeTruthy();
    expect(await screen.findByText('Ок', {}, { timeout: 3000 })).toBeTruthy();
    expect(screen.queryByText('Нет')).toBeNull();
  });
});
