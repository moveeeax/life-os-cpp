import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { api, apiErrorMessage, ApiClientError, newIdempotencyKey } from './client';

/**
 * The refresh-on-401 machinery is the most failure-prone logic in the SPA:
 * a regression here silently logs every user out after 15 minutes. These
 * tests drive it through a mocked global fetch.
 */

type FetchCall = { url: string; init: RequestInit };

function jsonResponse(status: number, body: unknown): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { 'Content-Type': 'application/json' },
  });
}

let calls: FetchCall[] = [];

beforeEach(() => {
  calls = [];
});

afterEach(() => {
  vi.unstubAllGlobals();
});

/** Install a fetch mock driven by a per-call handler list (in order). */
function mockFetchSequence(handlers: Array<(call: FetchCall) => Response | Promise<Response>>) {
  let i = 0;
  vi.stubGlobal(
    'fetch',
    vi.fn(async (url: string, init: RequestInit = {}) => {
      const call = { url: String(url), init };
      calls.push(call);
      const handler = handlers[Math.min(i, handlers.length - 1)];
      i += 1;
      return handler(call);
    }),
  );
}

describe('api client 401→refresh→retry', () => {
  it('refreshes once and replays the original request on 401', async () => {
    mockFetchSequence([
      () => jsonResponse(401, { error: 'missing_token' }), // original
      () => new Response(null, { status: 200 }), // POST /api/auth/refresh
      () => jsonResponse(200, { data: 'after-refresh' }), // replay
    ]);

    // Typed path: response type is inferred from the OpenAPI `paths` tree.
    // The mocked body is shape-agnostic here — we assert the runtime value.
    const { data, error } = await api.GET('/api/v1/jobs');
    expect(error).toBeUndefined();
    expect(data).toEqual({ data: 'after-refresh' });

    expect(calls.map((c) => c.url)).toEqual([
      '/api/v1/jobs',
      '/api/v1/auth/refresh',
      '/api/v1/jobs',
    ]);
    expect(calls[1].init.method).toBe('POST');
  });

  it('returns the original 401 when refresh fails', async () => {
    mockFetchSequence([
      () => jsonResponse(401, { error: 'missing_token' }),
      () => new Response(null, { status: 401 }), // refresh rejected
    ]);

    const { error } = await api.GET('/api/v1/jobs');
    expect(error?.status).toBe(401);
    // No replay after a failed refresh.
    expect(calls.map((c) => c.url)).toEqual(['/api/v1/jobs', '/api/v1/auth/refresh']);
  });

  it('never tries to refresh for the credential endpoints (login/register/refresh/logout)', async () => {
    for (const path of [
      '/api/v1/auth/login',
      '/api/v1/auth/register',
      '/api/v1/auth/refresh',
      '/api/v1/auth/logout',
    ]) {
      calls = [];
      mockFetchSequence([() => jsonResponse(401, { error: 'invalid_credentials' })]);
      const { error } = await api.POST(path, { body: {} });
      expect(error?.status).toBe(401);
      // No /api/auth/refresh follow-up — exactly the one original call.
      expect(calls.map((c) => c.url)).toEqual([path]);
    }
  });

  it('refreshes and replays /api/auth/me on 401 (idle-tab session resume)', async () => {
    // The regression this guards: /api/auth/me used to be excluded from the
    // refresh path, so an idle tab whose access cookie expired got a bare
    // 401, useMe() resolved to null, and the guard bounced a still-valid
    // session to /login. /me must refresh + replay like any other call.
    mockFetchSequence([
      () => jsonResponse(401, { error: 'missing_token' }), // /me — access expired
      () => new Response(null, { status: 200 }), // refresh succeeds
      () => jsonResponse(200, { user: { id: 'u1' } }), // replayed /me
    ]);

    const { data, error } = await api.GET('/api/v1/auth/me');
    expect(error).toBeUndefined();
    expect(data).toEqual({ user: { id: 'u1' } });
    expect(calls.map((c) => c.url)).toEqual([
      '/api/v1/auth/me',
      '/api/v1/auth/refresh',
      '/api/v1/auth/me',
    ]);
  });

  it('deduplicates concurrent refreshes: N parallel 401s → one refresh call', async () => {
    let refreshes = 0;
    vi.stubGlobal(
      'fetch',
      vi.fn(async (url: string, init: RequestInit = {}) => {
        calls.push({ url: String(url), init });
        if (String(url) === '/api/v1/auth/refresh') {
          refreshes += 1;
          // Slow refresh so all three 401 handlers race into tryRefresh().
          await new Promise((r) => setTimeout(r, 20));
          return new Response(null, { status: 200 });
        }
        // First hit per path: 401; replay after refresh: 200.
        const seen = calls.filter((c) => c.url === String(url)).length;
        return seen === 1
          ? jsonResponse(401, { error: 'missing_token' })
          : jsonResponse(200, { ok: true });
      }),
    );

    const [a, b, c] = await Promise.all([
      api.GET('/api/v1/jobs'),
      api.GET('/api/v1/admin/users'),
      api.GET('/api/v1/auth-adjacent'), // not /api/auth/ prefix — note the dash
    ]);
    expect(a.error).toBeUndefined();
    expect(b.error).toBeUndefined();
    expect(c.error).toBeUndefined();
    expect(refreshes).toBe(1);
  });

  it('serializes the body and sets Content-Type for JSON posts', async () => {
    mockFetchSequence([() => jsonResponse(200, { ok: true })]);
    await api.POST('/api/v1/jobs', { body: { type: 'echo' } });
    expect(calls[0].init.body).toBe(JSON.stringify({ type: 'echo' }));
    expect((calls[0].init.headers as Record<string, string>)['Content-Type']).toBe(
      'application/json',
    );
    expect(calls[0].init.credentials).toBe('include');
  });

  it('getJson throws the ApiError on failure', async () => {
    mockFetchSequence([() => jsonResponse(404, { error: 'not_found', status: 404 })]);
    await expect(api.getJson('/api/v1/auth/me')).rejects.toMatchObject({ status: 404 });
  });

  it('returns the second 401 after a successful refresh — exactly one replay, no loop', async () => {
    mockFetchSequence([
      () => jsonResponse(401, { error: 'missing_token' }), // original
      () => new Response(null, { status: 200 }), // refresh succeeds…
      () => jsonResponse(401, { error: 'missing_token' }), // …but the replay still 401s
    ]);

    const { error } = await api.GET('/api/v1/jobs');
    expect(error?.status).toBe(401);
    // original + refresh + one replay. No second refresh, no infinite loop.
    expect(calls.map((c) => c.url)).toEqual([
      '/api/v1/jobs',
      '/api/v1/auth/refresh',
      '/api/v1/jobs',
    ]);
  });
});

describe('api client failure modes', () => {
  it('wraps a fetch rejection into ApiClientError(status 0, "Network error")', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn(async () => {
        throw new TypeError('Failed to fetch');
      }),
    );

    const { data, error, response } = await api.GET('/api/v1/jobs');
    expect(data).toBeUndefined();
    expect(response).toBeUndefined();
    expect(error).toBeInstanceOf(ApiClientError);
    expect(error?.status).toBe(0);
    expect(error?.message).toBe('Network error');
  });

  it('getJson rejects with ApiClientError on network failure (no unhandled rejection)', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn(async () => {
        throw new TypeError('Failed to fetch');
      }),
    );

    await expect(api.getJson('/api/v1/jobs')).rejects.toBeInstanceOf(ApiClientError);
    await expect(api.getJson('/api/v1/jobs')).rejects.toMatchObject({
      status: 0,
      message: 'Network error',
    });
  });

  it('handles a non-JSON error body: message carries the raw text', async () => {
    mockFetchSequence([
      () =>
        new Response('<html>502 Bad Gateway</html>', {
          status: 502,
          statusText: 'Bad Gateway',
          headers: { 'Content-Type': 'text/html' },
        }),
    ]);

    const { error } = await api.GET('/api/v1/jobs');
    expect(error).toBeInstanceOf(ApiClientError);
    expect(error?.status).toBe(502);
    expect(error?.message).toBe('<html>502 Bad Gateway</html>');
    expect(apiErrorMessage(error)).toBe('<html>502 Bad Gateway</html>');
  });

  it('exposes the backend envelope as code/fields and via apiErrorMessage', async () => {
    mockFetchSequence([
      () =>
        jsonResponse(400, {
          error: 'validation_failed',
          errors: [{ field: 'email', code: 'bad_format', message: 'expected format: email' }],
        }),
    ]);

    const { error } = await api.GET('/api/v1/jobs');
    expect(error).toBeInstanceOf(ApiClientError);
    expect(error?.code).toBe('validation_failed');
    expect(error?.fields).toHaveLength(1);
    expect(apiErrorMessage(error)).toBe('email: expected format: email');
  });
});

describe('Idempotency-Key', () => {
  const keyOf = (call: FetchCall): string | undefined =>
    (call.init.headers as Record<string, string>)['Idempotency-Key'];

  it('is sent on every mutating method and never on a GET', async () => {
    mockFetchSequence([() => jsonResponse(200, { ok: true })]);
    await api.GET('/api/v1/jobs');
    expect(keyOf(calls[0])).toBeUndefined();

    for (const verb of ['POST', 'PUT', 'PATCH', 'DELETE'] as const) {
      calls = [];
      mockFetchSequence([() => jsonResponse(200, { ok: true })]);
      await api[verb]('/api/v1/jobs', { body: {} });
      expect(keyOf(calls[0]), verb).toMatch(/^[0-9a-f-]{32,36}$/);
    }
  });

  it('mints a different key per call', async () => {
    mockFetchSequence([() => jsonResponse(200, { ok: true })]);
    await api.POST('/api/v1/jobs', { body: { type: 'echo' } });
    await api.POST('/api/v1/jobs', { body: { type: 'echo' } });
    expect(keyOf(calls[0])).not.toBe(keyOf(calls[1]));
  });

  it('replays the same key after a 401 + refresh', async () => {
    mockFetchSequence([
      () => jsonResponse(401, { error: 'missing_token' }), // original POST
      () => new Response(null, { status: 200 }), // refresh
      () => jsonResponse(200, { ok: true }), // replay
    ]);
    const { error } = await api.POST('/api/v1/jobs', { body: { type: 'echo' } });
    expect(error).toBeUndefined();
    expect(calls.map((c) => c.url)).toEqual([
      '/api/v1/jobs',
      '/api/v1/auth/refresh',
      '/api/v1/jobs',
    ]);
    const first = keyOf(calls[0]);
    expect(first).toBeDefined();
    expect(keyOf(calls[2])).toBe(first);
    // The refresh is its own call, not a replay of anything: no key.
    expect(keyOf(calls[1])).toBeUndefined();
  });

  it('keeps a key the caller supplied, whatever its case', async () => {
    mockFetchSequence([() => jsonResponse(200, { ok: true })]);
    await api.POST('/api/v1/jobs', { body: {}, headers: { 'Idempotency-Key': 'mine-1' } });
    expect(keyOf(calls[0])).toBe('mine-1');

    calls = [];
    mockFetchSequence([() => jsonResponse(200, { ok: true })]);
    await api.POST('/api/v1/jobs', { body: {}, headers: { 'idempotency-key': 'mine-2' } });
    const headers = calls[0].init.headers as Record<string, string>;
    expect(headers['idempotency-key']).toBe('mine-2');
    expect(headers['Idempotency-Key']).toBeUndefined();
  });

  it('falls back to 32 hex digits without crypto (a plain-HTTP LAN page)', () => {
    vi.stubGlobal('crypto', undefined);
    const key = newIdempotencyKey();
    expect(key).toMatch(/^[0-9a-f]{32}$/);
    expect(newIdempotencyKey()).not.toBe(key);
  });

  it('uses getRandomValues when randomUUID is missing (no secure context)', () => {
    vi.stubGlobal('crypto', {
      getRandomValues: (a: Uint8Array) => {
        a.fill(0xab);
        return a;
      },
    });
    expect(newIdempotencyKey()).toBe('ab'.repeat(16));
  });
});

describe('apiErrorMessage', () => {
  it('prefers field-level validation details', () => {
    const msg = apiErrorMessage({
      status: 400,
      error: 'validation_failed',
      errors: [
        { field: 'email', code: 'bad_format', message: 'expected format: email' },
        { field: 'password', code: 'too_short', message: 'min length 8' },
      ],
    });
    expect(msg).toBe('email: expected format: email; password: min length 8');
  });

  it('falls back to message, then error code, then the default', () => {
    expect(apiErrorMessage({ status: 500, message: 'boom' })).toBe('boom');
    expect(apiErrorMessage({ status: 500, error: 'internal_error' })).toBe('internal_error');
    expect(apiErrorMessage(undefined)).toBe('Something went wrong');
    expect(apiErrorMessage(null, 'custom')).toBe('custom');
  });
});

describe('a 204 No Content answer', () => {
  it('resolves deleteJson instead of throwing on the empty body', async () => {
    mockFetchSequence([() => new Response(null, { status: 204 })]);
    await expect(api.deleteJson('/api/v1/tasks/items/abc')).resolves.toBeUndefined();
  });

  it('still refuses an empty 200, where a body was due', async () => {
    mockFetchSequence([() => new Response('', { status: 200 })]);
    await expect(api.getJson('/api/v1/tasks/items')).rejects.toThrow(/empty body/);
  });
});

describe('query strings', () => {
  it('appends the query, drops null and undefined, keeps a query already in the path', async () => {
    mockFetchSequence([() => jsonResponse(200, { ok: true })]);
    await api.getJson('/api/v1/things?a=1', {
      query: { limit: 20, offset: 0, q: 'coffee & tea', skip: undefined, none: null, on: false },
    });
    expect(calls[0].url).toBe('/api/v1/things?a=1&limit=20&offset=0&q=coffee+%26+tea&on=false');
  });

  it('leaves the path alone without a query', async () => {
    mockFetchSequence([
      () => jsonResponse(200, { ok: true }),
      () => jsonResponse(200, { ok: true }),
    ]);
    await api.getJson('/api/v1/things');
    await api.getJson('/api/v1/things', { query: { skip: undefined } });
    expect(calls.map((c) => c.url)).toEqual(['/api/v1/things', '/api/v1/things']);
  });
});
