// A small markdown reader for the advisor's text: headings, lists, paragraphs
// and **bold**. It returns plain data, rendered by React, so nothing in the
// model's text is ever interpreted as HTML.

export type Inline = { text: string; bold: boolean };
export type Block =
  | { kind: 'heading'; level: 2 | 3; parts: Inline[] }
  | { kind: 'list'; items: Inline[][] }
  | { kind: 'paragraph'; parts: Inline[] };

export function inline(text: string): Inline[] {
  const out: Inline[] = [];
  const re = /\*\*(.+?)\*\*/g;
  let last = 0;
  for (let m = re.exec(text); m; m = re.exec(text)) {
    if (m.index > last) out.push({ text: text.slice(last, m.index), bold: false });
    out.push({ text: m[1], bold: true });
    last = m.index + m[0].length;
  }
  if (last < text.length) out.push({ text: text.slice(last), bold: false });
  return out;
}

export function parseMarkdown(source: string): Block[] {
  const blocks: Block[] = [];
  let paragraph: string[] = [];
  let list: Inline[][] | null = null;
  const flush = () => {
    if (paragraph.length) blocks.push({ kind: 'paragraph', parts: inline(paragraph.join(' ')) });
    paragraph = [];
    if (list) blocks.push({ kind: 'list', items: list });
    list = null;
  };
  for (const raw of source.replace(/\r\n/g, '\n').split('\n')) {
    const line = raw.trim();
    const heading = /^(#{1,3})\s+(.*)$/.exec(line);
    const item = /^[-*]\s+(.*)$/.exec(line) ?? /^\d+[.)]\s+(.*)$/.exec(line);
    if (line === '') {
      flush();
    } else if (heading) {
      flush();
      blocks.push({
        kind: 'heading',
        level: heading[1].length >= 3 ? 3 : 2,
        parts: inline(heading[2]),
      });
    } else if (item) {
      if (paragraph.length) {
        blocks.push({ kind: 'paragraph', parts: inline(paragraph.join(' ')) });
        paragraph = [];
      }
      (list ??= []).push(inline(item[1]));
    } else {
      if (list) {
        blocks.push({ kind: 'list', items: list });
        list = null;
      }
      paragraph.push(line);
    }
  }
  flush();
  return blocks;
}
