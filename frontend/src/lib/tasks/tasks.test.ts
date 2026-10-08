import { describe, expect, it } from 'vitest';

import { dueLabel, parseErrorText, safeLink } from './index';

describe('dueLabel', () => {
  const today = '2026-10-07';
  it('names late, today, tomorrow and a later day', () => {
    expect(dueLabel('2026-10-04', today)).toEqual({ text: '3 days late', tone: 'late' });
    expect(dueLabel('2026-10-06', today)).toEqual({ text: '1 day late', tone: 'late' });
    expect(dueLabel('2026-10-07', today)).toEqual({ text: 'today', tone: 'today' });
    expect(dueLabel('2026-10-08', today)).toEqual({ text: 'tomorrow', tone: 'plain' });
    expect(dueLabel('2026-10-12', today)).toEqual({ text: '12 Oct', tone: 'plain' });
  });
  it('has nothing to say without a due', () => {
    expect(dueLabel(null, today)).toBeNull();
  });
  it('counts calendar days across a DST change', () => {
    expect(dueLabel('2026-10-26', '2026-10-24')?.text).toBe('26 Oct');
  });
});

describe('safeLink', () => {
  it('keeps http(s) links and drops every other scheme', () => {
    expect(safeLink('https://mail.example/1')).toBe('https://mail.example/1');
    expect(safeLink('javascript:alert(1)')).toBeNull();
    expect(safeLink('JaVaScRiPt:alert(1)')).toBeNull();
    expect(safeLink('data:text/html,<script>1</script>')).toBeNull();
    expect(safeLink('not a url')).toBeNull();
    expect(safeLink(null)).toBeNull();
  });
});

describe('parseErrorText', () => {
  it('says what went wrong without the raw code', () => {
    expect(parseErrorText('invalid_answer: a line has an area outside the six: work')).toBe(
      'The reader answered something that is not a task.',
    );
    expect(parseErrorText('not_configured')).toBe('The phrase reader is not set up.');
    expect(parseErrorText('something_new: x')).toBe('The phrase could not be read.');
    expect(parseErrorText(null)).toBe('The phrase could not be read.');
  });
});
