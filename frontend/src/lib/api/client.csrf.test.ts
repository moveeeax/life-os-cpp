// @vitest-environment jsdom
import { afterEach, describe, expect, it } from 'vitest';

import { csrfHeader } from './client';

/**
 * The double-submit CSRF header: read from the non-HttpOnly cookie and sent
 * on state-changing methods only. jsdom gives this file a document.cookie.
 */

function clearCookies() {
  for (const part of document.cookie.split('; ')) {
    const name = part.split('=')[0];
    if (name) document.cookie = `${name}=; expires=Thu, 01 Jan 1970 00:00:00 GMT`;
  }
}

afterEach(clearCookies);

describe('csrfHeader', () => {
  it('is empty without the cookie', () => {
    expect(csrfHeader('POST')).toEqual({});
  });

  it('echoes the cookie on unsafe methods only', () => {
    document.cookie = 'csrf-token=abc123';
    expect(csrfHeader('POST')).toEqual({ 'X-CSRF-Token': 'abc123' });
    expect(csrfHeader('put')).toEqual({ 'X-CSRF-Token': 'abc123' });
    expect(csrfHeader('PATCH')).toEqual({ 'X-CSRF-Token': 'abc123' });
    expect(csrfHeader('DELETE')).toEqual({ 'X-CSRF-Token': 'abc123' });
    expect(csrfHeader('GET')).toEqual({});
    expect(csrfHeader('HEAD')).toEqual({});
  });

  it('finds the cookie among others and decodes it', () => {
    document.cookie = 'theme=dark';
    document.cookie = 'csrf-token=a%2Fb%3Dc';
    document.cookie = 'other-csrf-token=nope';
    expect(csrfHeader('POST')).toEqual({ 'X-CSRF-Token': 'a/b=c' });
  });

  it('a cookie whose name only ends with the right name is not it', () => {
    document.cookie = 'xcsrf-token=wrong';
    expect(csrfHeader('POST')).toEqual({});
  });
});
