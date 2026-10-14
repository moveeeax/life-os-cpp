// The Markdown of a note read into plain data that React renders, so nothing
// a note holds is ever interpreted as HTML: headings, paragraphs, lists,
// quotes, fenced code; inline **bold**, *italic*, `code` and [links](https://…)
// (a link that is not http(s) stays text).

export type Span =
  | { kind: 'text'; text: string }
  | { kind: 'bold'; text: string }
  | { kind: 'italic'; text: string }
  | { kind: 'code'; text: string }
  | { kind: 'link'; text: string; href: string };

export type NoteBlock =
  | { kind: 'heading'; level: 1 | 2 | 3; parts: Span[] }
  | { kind: 'paragraph'; parts: Span[] }
  | { kind: 'list'; items: Span[][] }
  | { kind: 'olist'; items: Span[][] }
  | { kind: 'quote'; parts: Span[] }
  | { kind: 'code'; lang: string; text: string };

const INLINE =
  /`([^`]+)`|\*\*(.+?)\*\*|\*([^*\s][^*]*?)\*|_([^_\s][^_]*?)_|\[([^\]]+)\]\(([^)\s]+)\)/g;

/** The inline marks of one line of text. */
export function spans(text: string): Span[] {
  const out: Span[] = [];
  let last = 0;
  const push = (s: Span) => {
    const prev = out[out.length - 1];
    if (s.kind === 'text' && prev?.kind === 'text') prev.text += s.text;
    else out.push(s);
  };
  for (let m = INLINE.exec(text); m; m = INLINE.exec(text)) {
    if (m.index > last) push({ kind: 'text', text: text.slice(last, m.index) });
    if (m[1] !== undefined) push({ kind: 'code', text: m[1] });
    else if (m[2] !== undefined) push({ kind: 'bold', text: m[2] });
    else if (m[3] !== undefined || m[4] !== undefined) push({ kind: 'italic', text: m[3] ?? m[4] });
    else if (/^https?:\/\//i.test(m[6])) push({ kind: 'link', text: m[5], href: m[6] });
    else push({ kind: 'text', text: m[0] });
    last = m.index + m[0].length;
  }
  if (last < text.length) push({ kind: 'text', text: text.slice(last) });
  INLINE.lastIndex = 0;
  return out;
}

/** The blocks of a note body. */
export function parseNote(source: string): NoteBlock[] {
  const blocks: NoteBlock[] = [];
  const lines = source.replace(/\r\n/g, '\n').split('\n');
  let para: string[] = [];
  let quote: string[] = [];
  let list: { kind: 'list' | 'olist'; items: Span[][] } | null = null;
  const flush = () => {
    if (para.length) blocks.push({ kind: 'paragraph', parts: spans(para.join(' ')) });
    if (quote.length) blocks.push({ kind: 'quote', parts: spans(quote.join(' ')) });
    if (list) blocks.push(list);
    para = [];
    quote = [];
    list = null;
  };
  for (let i = 0; i < lines.length; i++) {
    const raw = lines[i];
    const line = raw.trim();
    const fence = /^```\s*([\w+-]*)\s*$/.exec(line);
    if (fence) {
      flush();
      const body: string[] = [];
      for (i++; i < lines.length && !/^```\s*$/.test(lines[i].trim()); i++) body.push(lines[i]);
      blocks.push({ kind: 'code', lang: fence[1], text: body.join('\n') });
      continue;
    }
    const heading = /^(#{1,3})\s+(.*)$/.exec(line);
    const bullet = /^[-*]\s+(.*)$/.exec(line);
    const numbered = /^\d+[.)]\s+(.*)$/.exec(line);
    const quoted = /^>\s?(.*)$/.exec(line);
    if (line === '') {
      flush();
    } else if (heading) {
      flush();
      blocks.push({
        kind: 'heading',
        level: heading[1].length as 1 | 2 | 3,
        parts: spans(heading[2]),
      });
    } else if (bullet || numbered) {
      const kind = bullet ? 'list' : 'olist';
      if (para.length || quote.length || (list && list.kind !== kind)) flush();
      list = list ?? { kind, items: [] };
      list.items.push(spans((bullet ?? numbered)![1]));
    } else if (quoted) {
      if (para.length || list) flush();
      quote.push(quoted[1]);
    } else {
      if (list || quote.length) flush();
      para.push(line);
    }
  }
  flush();
  return blocks;
}
