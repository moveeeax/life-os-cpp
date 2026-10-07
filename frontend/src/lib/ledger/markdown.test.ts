import { describe, expect, it } from 'vitest';

import { inline, parseMarkdown } from './markdown';

describe('parseMarkdown', () => {
  it('reads headings, lists and paragraphs', () => {
    const blocks = parseMarkdown(
      '## What happened\nRent took **KZT 250,000**.\n\n- one\n- two\n\n### Next\n1. Check',
    );
    expect(blocks.map((b) => b.kind)).toEqual(['heading', 'paragraph', 'list', 'heading', 'list']);
    expect(blocks[1]).toEqual({
      kind: 'paragraph',
      parts: [
        { text: 'Rent took ', bold: false },
        { text: 'KZT 250,000', bold: true },
        { text: '.', bold: false },
      ],
    });
    expect(blocks[2]).toMatchObject({
      kind: 'list',
      items: [[{ text: 'one' }], [{ text: 'two' }]],
    });
  });
  it('keeps HTML as text', () => {
    const blocks = parseMarkdown('<script>alert(1)</script>');
    expect(blocks).toEqual([
      { kind: 'paragraph', parts: [{ text: '<script>alert(1)</script>', bold: false }] },
    ]);
  });
  it('joins wrapped lines of a paragraph', () => {
    expect(parseMarkdown('a\nb')).toEqual([
      { kind: 'paragraph', parts: [{ text: 'a b', bold: false }] },
    ]);
  });
  it('leaves an unclosed bold as text', () => {
    expect(inline('**half')).toEqual([{ text: '**half', bold: false }]);
  });
});
