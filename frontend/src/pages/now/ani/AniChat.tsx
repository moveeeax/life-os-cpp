import { useEffect, useRef, useState, type FormEvent, type KeyboardEvent } from 'react';

import {
  POLL_MS,
  TEXT_MAX,
  failure,
  isOpen,
  loadTurns,
  readTurn,
  refusal,
  sendMessage,
  wakeAgent,
  type Turn,
} from './chat';

/**
 * The chat with Ani's agent, right of her on Now. Opening it wakes her agent,
 * so it is usually up by the first message; the reply of an open turn is
 * polled every second. Messages and replies are personal: Hide numbers blurs
 * them.
 */
export function AniChat({ tz, onClose }: { tz: string; onClose(): void }) {
  const [turns, setTurns] = useState<Turn[] | null>(null);
  const [loadFailed, setLoadFailed] = useState(false);
  const [text, setText] = useState('');
  const [sending, setSending] = useState(false);
  const [problem, setProblem] = useState<string | null>(null);
  const listRef = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLTextAreaElement>(null);

  useEffect(() => {
    wakeAgent();
    let gone = false;
    loadTurns()
      .then((t) => !gone && setTurns(t))
      .catch(() => !gone && setLoadFailed(true));
    inputRef.current?.focus();
    return () => {
      gone = true;
    };
  }, []);

  const last = turns?.[turns.length - 1];
  const waiting = last != null && isOpen(last);

  // The open turn, read again until it ends.
  useEffect(() => {
    if (!last || !isOpen(last)) return;
    const id = last.id;
    const timer = window.setInterval(() => {
      readTurn(id)
        .then((t) => setTurns((all) => all?.map((x) => (x.id === t.id ? t : x)) ?? all))
        .catch(() => {});
    }, POLL_MS);
    return () => window.clearInterval(timer);
  }, [last?.id, last?.status]); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    const el = listRef.current;
    if (el) el.scrollTop = el.scrollHeight;
  }, [turns]);

  useEffect(() => {
    const onKey = (e: globalThis.KeyboardEvent) => {
      if (e.key === 'Escape') onClose();
    };
    document.addEventListener('keydown', onKey);
    return () => document.removeEventListener('keydown', onKey);
  }, [onClose]);

  const send = async (e?: FormEvent) => {
    e?.preventDefault();
    const t = text.trim();
    if (!t || sending || waiting) return;
    setSending(true);
    setProblem(null);
    try {
      const turn = await sendMessage(t, tz);
      setTurns((all) => [...(all ?? []), turn]);
      setText('');
    } catch (err) {
      setProblem(refusal(err));
    } finally {
      setSending(false);
      inputRef.current?.focus();
    }
  };

  const onKeyDown = (e: KeyboardEvent<HTMLTextAreaElement>) => {
    if (e.key === 'Enter' && !e.shiftKey && !e.nativeEvent.isComposing) {
      e.preventDefault();
      void send();
    }
  };

  return (
    <section className="ani-chat" aria-label="Chat with Ani">
      <header className="ani-chat-head">
        <span className="ani-k">
          <span className="ani-dot" />
          Ani
        </span>
        <button
          type="button"
          className="ani-chat-close"
          aria-label="Close the chat"
          onClick={onClose}
        >
          ×
        </button>
      </header>
      <div ref={listRef} className="ani-chat-list" aria-live="polite">
        {loadFailed ? (
          <p className="ani-chat-note">The chat did not load. Close it and open again.</p>
        ) : turns == null ? (
          <p className="ani-chat-note">Loading</p>
        ) : turns.length === 0 ? (
          <p className="ani-chat-note">
            Write to her. She can look at your day, tasks, goals, habits, sleep, money and food, but
            cannot change anything yet.
          </p>
        ) : (
          turns.map((t) => (
            <div key={t.id} className="ani-turn">
              <p className="ani-msg ani-msg-you num">{t.text}</p>
              {t.status === 'done' ? (
                <p className="ani-msg ani-msg-her num">{t.reply}</p>
              ) : t.status === 'failed' ? (
                <p className="ani-msg ani-msg-fail">{failure(t.error)}</p>
              ) : (
                <p className="ani-msg ani-msg-her ani-msg-wait" role="status">
                  {t.status === 'queued' ? 'Waking up' : 'Thinking'}
                  <span className="ani-dots" aria-hidden="true" />
                </p>
              )}
            </div>
          ))
        )}
      </div>
      {problem ? (
        <p className="ani-chat-problem" role="alert">
          {problem}
        </p>
      ) : null}
      <form className="ani-chat-form" onSubmit={(e) => void send(e)}>
        <textarea
          ref={inputRef}
          className="num"
          rows={1}
          value={text}
          maxLength={TEXT_MAX}
          placeholder={waiting ? 'She is answering' : 'Message'}
          aria-label="Message to Ani"
          onChange={(e) => setText(e.target.value)}
          onKeyDown={onKeyDown}
        />
        <button
          type="submit"
          className="ani-chat-send"
          aria-label="Send"
          disabled={!text.trim() || sending || waiting}
        >
          <svg
            viewBox="0 0 24 24"
            width="16"
            height="16"
            aria-hidden="true"
            fill="none"
            stroke="currentColor"
            strokeWidth="2"
            strokeLinecap="round"
            strokeLinejoin="round"
          >
            <path d="M12 19V5M5.5 11.5 12 5l6.5 6.5" />
          </svg>
        </button>
      </form>
      <p className="ani-chat-credit" data-num-ok>
        An AI agent (OpenAI) answers here and reads your Life OS to do it.
      </p>
    </section>
  );
}
