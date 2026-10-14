import { describe, expect, it } from 'vitest';

import { ApiClientError } from '@/lib/api/client';

import { afterPoll, enqueueErrorText, pollErrorText } from './parseJob';

describe('afterPoll', () => {
  it('done ends the poll', () => {
    expect(afterPoll({ status: 'done' })).toEqual({ state: 'done' });
  });

  it("failed ends it with the job's error", () => {
    expect(afterPoll({ status: 'failed', error: 'provider_unavailable: 503' })).toEqual({
      state: 'failed',
      error: 'provider_unavailable: 503',
    });
    expect(afterPoll({ status: 'failed' })).toEqual({ state: 'failed', error: null });
  });

  it('queued and running keep polling', () => {
    expect(afterPoll({ status: 'queued' })).toBeNull();
    expect(afterPoll({ status: 'running' })).toBeNull();
  });
});

describe('enqueueErrorText', () => {
  it("says the module's own words when the reader is not configured", () => {
    const e = new ApiClientError({ status: 503, message: 'x', code: 'not_configured' });
    expect(enqueueErrorText(e, 'not_configured')).toBe('not_configured');
    expect(enqueueErrorText(e, 'Text parsing is not configured on the server.')).toBe(
      'Text parsing is not configured on the server.',
    );
  });

  it("otherwise the server's message", () => {
    const e = new ApiClientError({ status: 429, message: 'too many', code: 'too_many_parses' });
    expect(enqueueErrorText(e, 'not_configured')).toBe('too many');
    expect(enqueueErrorText(new Error('boom'), 'not_configured')).toBe('boom');
  });
});

describe('pollErrorText', () => {
  it('a 404 ends the poll: the job is gone', () => {
    expect(pollErrorText(new ApiClientError({ status: 404, message: 'not found' }))).toBe(
      'The parse job is gone.',
    );
  });

  it('a dropped connection or a 5xx keeps polling', () => {
    expect(pollErrorText(new ApiClientError({ status: 0, message: 'Network error' }))).toBeNull();
    expect(pollErrorText(new ApiClientError({ status: 503, message: 'busy' }))).toBeNull();
    expect(pollErrorText(new Error('x'))).toBeNull();
  });
});
