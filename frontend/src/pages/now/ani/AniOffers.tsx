import { useEffect, useLayoutEffect, useRef, useState } from 'react';

import { cancelAction, chatOf, confirmAction, editable, readTurn, type Action } from './chat';

/** How long "Sent to …" stays before the bubble goes. */
const SENT_MS = 2000;

/**
 * What her answer proposes, in a thought bubble at her head: the line that
 * brought it on top, Telegram reply drafts in full (one tap sends), an empty
 * field for an own reply, other changes with Yes and No, and Accept all for
 * two or more. Later folds the bubble into a puff with the chat's letter;
 * a tap opens it again. While she says another line (`hushed`) the bubble
 * folds too, unless the person is writing. Nothing changes before a tap.
 */
export function AniOffers({
  turn,
  line = null,
  hushed = false,
  onDone,
  onWriting,
}: {
  turn: string;
  line?: string | null;
  hushed?: boolean;
  onDone: () => void;
  onWriting?: (writing: boolean) => void;
}) {
  const [actions, setActions] = useState<Action[] | null>(null);
  const [editing, setEditing] = useState<{ id: string; text: string } | null>(null);
  const [busy, setBusy] = useState(false);
  const [folded, setFolded] = useState(false);
  const [sent, setSent] = useState<string | null>(null);
  const [beside, setBeside] = useState(false);
  const box = useRef<HTMLDivElement>(null);
  // The corner passes new callbacks on every render; the timers keep the latest.
  const done = useRef(onDone);
  done.current = onDone;
  useEffect(() => {
    let live = true;
    readTurn(turn)
      .then((t) => live && setActions(t.actions.filter((a) => a.status === 'pending')))
      .catch(() => live && setActions([]));
    return () => {
      live = false;
    };
  }, [turn]);
  useEffect(() => {
    if (actions && actions.length === 0 && !sent) done.current();
  }, [actions, sent]);
  useEffect(() => {
    if (!sent) return;
    const t = window.setTimeout(() => done.current(), SENT_MS);
    return () => clearTimeout(t);
  }, [sent]);
  useEffect(() => onWriting?.(editing !== null), [editing, onWriting]);
  // Above her head when there is room under the nav; beside it otherwise.
  useLayoutEffect(() => {
    const el = box.current;
    if (!el) return;
    const head = parseFloat(getComputedStyle(el).getPropertyValue('--ani-hy')) || 0;
    setBeside(head > 0 && el.scrollHeight > head - 110);
  }, [actions, editing, line, folded, hushed]);

  if (sent) {
    return (
      <div className="ani-think ani-think-sent" role="status">
        Sent to {sent}
      </div>
    );
  }
  if (!actions || actions.length === 0) return null;

  const drafts = actions.filter((a) => a.kind === 'tg_send');
  const changes = actions.filter((a) => a.kind !== 'tg_send');
  const chat = drafts.length > 0 ? chatOf(drafts[0].summary) : null;
  const run = async (f: () => Promise<unknown>) => {
    setBusy(true);
    try {
      await f();
    } catch {
      // The chat shows what became of it.
    }
    setBusy(false);
  };
  const confirm = (a: Action, text?: string) =>
    run(async () => {
      await (text === undefined ? confirmAction(a.id) : confirmAction(a.id, text));
      // A reply was picked: the other drafts to that chat are gone on the server too.
      setActions((all) =>
        (all ?? []).filter((x) => x.id !== a.id && !(a.kind === 'tg_send' && x.kind === 'tg_send')),
      );
      setEditing(null);
      if (a.kind === 'tg_send') setSent(chatOf(a.summary) ?? 'the chat');
    });
  const cancel = (a: Action) =>
    run(async () => {
      await cancelAction(a.id);
      setActions((all) => (all ?? []).filter((x) => x.id !== a.id));
    });

  if ((folded || hushed) && !editing) {
    const who = chat ?? 'Ani';
    return (
      <button
        type="button"
        className="ani-think-puff"
        aria-label={chat ? `Reply to ${chat}` : 'What she proposes'}
        onClick={() => setFolded(false)}
      >
        {who.slice(0, 1).toUpperCase()}
      </button>
    );
  }

  return (
    <>
      <span className="ani-think-puffs" data-beside={beside ? '' : undefined} aria-hidden="true">
        <i />
        <i />
      </span>
      <div
        ref={box}
        className="ani-think"
        data-beside={beside ? '' : undefined}
        role="group"
        aria-label="What she proposes"
      >
        {line && (
          <p className="ani-think-line" data-num-ok>
            {line}
          </p>
        )}
        {editing ? (
          <div className="ani-offer-edit">
            <textarea
              aria-label="The message"
              className="num"
              value={editing.text}
              maxLength={4000}
              rows={3}
              autoFocus
              placeholder={chat ? `To ${chat}` : undefined}
              onChange={(e) => setEditing({ ...editing, text: e.target.value })}
              onKeyDown={(e) => {
                if (e.key === 'Enter' && !e.shiftKey) {
                  e.preventDefault();
                  const a = actions.find((x) => x.id === editing.id);
                  if (a && editing.text.trim()) void confirm(a, editing.text);
                }
              }}
            />
            <span className="ani-offer-row">
              <button
                type="button"
                className="ani-offer-main"
                disabled={busy || editing.text.trim() === ''}
                onClick={() => {
                  const a = actions.find((x) => x.id === editing.id);
                  if (a) void confirm(a, editing.text);
                }}
              >
                Send
              </button>
              <button type="button" onClick={() => setEditing(null)}>
                Back
              </button>
            </span>
          </div>
        ) : (
          <>
            {drafts.length > 0 && (
              <div className="ani-think-drafts">
                {drafts.map((a) => (
                  <button
                    key={a.id}
                    type="button"
                    className="ani-think-draft"
                    disabled={busy}
                    onClick={() => void confirm(a)}
                  >
                    <span className="num">{a.text ?? a.summary}</span>
                    <svg viewBox="0 0 16 16" aria-hidden="true">
                      <path d="M2 8h10M8 3.5 12.5 8 8 12.5" />
                    </svg>
                  </button>
                ))}
              </div>
            )}
            {changes.map((a) => (
              <span key={a.id} className="ani-offer-row">
                <span className="ani-offer-text num">{a.summary}</span>
                <button
                  type="button"
                  className="ani-offer-main"
                  disabled={busy}
                  onClick={() => void confirm(a)}
                >
                  Yes
                </button>
                <button type="button" disabled={busy} onClick={() => void cancel(a)}>
                  No
                </button>
              </span>
            ))}
            <span className="ani-offer-row">
              {changes.length > 1 && (
                <button
                  type="button"
                  className="ani-offer-main"
                  disabled={busy}
                  onClick={() =>
                    void run(async () => {
                      for (const a of changes) await confirmAction(a.id).catch(() => undefined);
                      setActions((all) => (all ?? []).filter((x) => x.kind === 'tg_send'));
                    })
                  }
                >
                  Accept all ({changes.length})
                </button>
              )}
              {drafts.length > 0 && editable(drafts[0]) && (
                <button type="button" onClick={() => setEditing({ id: drafts[0].id, text: '' })}>
                  Own reply…
                </button>
              )}
              <button type="button" className="ani-think-later" onClick={() => setFolded(true)}>
                Later
              </button>
            </span>
          </>
        )}
      </div>
    </>
  );
}
