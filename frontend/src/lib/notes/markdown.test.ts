import { describe, expect, it } from 'vitest';

import { parseNote, spans } from './markdown';

describe('spans', () => {
  it('reads bold, italic, code and links', () => {
    expect(spans('a **b** *c* `d` [e](https://x.io)')).toEqual([
      { kind: 'text', text: 'a ' },
      { kind: 'bold', text: 'b' },
      { kind: 'text', text: ' ' },
      { kind: 'italic', text: 'c' },
      { kind: 'text', text: ' ' },
      { kind: 'code', text: 'd' },
      { kind: 'text', text: ' ' },
      { kind: 'link', text: 'e', href: 'https://x.io' },
    ]);
  });
  it('keeps a link that is not http(s) as plain text', () => {
    expect(spans('[x](javascript:alert(1))')).toEqual([
      { kind: 'text', text: '[x](javascript:alert(1))' },
    ]);
  });
  it('leaves HTML as text', () => {
    expect(spans('<script>alert(1)</script>')).toEqual([
      { kind: 'text', text: '<script>alert(1)</script>' },
    ]);
  });
  it('code wins over the marks inside it', () => {
    expect(spans('`**not bold**`')).toEqual([{ kind: 'code', text: '**not bold**' }]);
  });
});

describe('parseNote', () => {
  it('reads headings, paragraphs, lists, quotes and fenced code', () => {
    const blocks = parseNote(
      [
        '# Title',
        '',
        'First line',
        'second line',
        '',
        '- one',
        '- two',
        '',
        '1. first',
        '2. second',
        '',
        '> a quote',
        '',
        '```ts',
        'const x = 1;',
        '',
        '# not a heading',
        '```',
      ].join('\n'),
    );
    expect(blocks.map((b) => b.kind)).toEqual([
      'heading',
      'paragraph',
      'list',
      'olist',
      'quote',
      'code',
    ]);
    expect(blocks[0]).toMatchObject({ kind: 'heading', level: 1 });
    expect(blocks[1]).toMatchObject({
      kind: 'paragraph',
      parts: [{ kind: 'text', text: 'First line second line' }],
    });
    expect(blocks[5]).toEqual({
      kind: 'code',
      lang: 'ts',
      text: 'const x = 1;\n\n# not a heading',
    });
  });
  it('an unclosed fence runs to the end', () => {
    expect(parseNote('```\nhalf')).toEqual([{ kind: 'code', lang: '', text: 'half' }]);
  });
});

import { monthOf, tagsFrom } from './index';

describe('tagsFrom', () => {
  it('splits, trims and drops repeats', () => {
    expect(tagsFrom('otel, tracing , otel,')).toEqual(['otel', 'tracing']);
    expect(tagsFrom('  ')).toEqual([]);
  });
});

describe('monthOf', () => {
  it('names the month', () => {
    expect(monthOf('2026-10-12T08:00:00+00:00')).toBe('October 2026');
  });
});
