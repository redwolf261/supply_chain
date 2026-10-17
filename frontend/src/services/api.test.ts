import { describe, expect, it } from 'vitest';
import { ApiError, createApi } from './api';

/** A fake server: records requests, replies with the next queued response. */
function fakeFetch(replies: Array<{ status?: number; body?: unknown; text?: string } | Error>) {
  const calls: Array<{ url: string; init?: RequestInit }> = [];
  const fn = async (url: string, init?: RequestInit) => {
    calls.push({ url, init });
    const reply = replies.shift();
    if (!reply) throw new Error('unexpected request');
    if (reply instanceof Error) throw reply;
    const status = reply.status ?? 200;
    return new Response(reply.text ?? JSON.stringify(reply.body ?? {}), { status });
  };
  return { fn, calls };
}

describe('api client', () => {
  it('sends JSON bodies with the right method, path and header', async () => {
    const { fn, calls } = fakeFetch([{ body: { network: { nodes: [], edges: [] } } }]);
    const api = createApi(fn, '');
    await api.loadScenario({ standard: 'supplier_failure', seed: 3 });
    expect(calls[0].url).toBe('/api/scenario/load');
    expect(calls[0].init?.method).toBe('POST');
    expect(JSON.parse(String(calls[0].init?.body))).toEqual({ standard: 'supplier_failure', seed: 3 });
    expect((calls[0].init?.headers as Record<string, string>)['Content-Type']).toBe('application/json');
  });

  it('prefixes the base URL and does not send a body on GET', async () => {
    const { fn, calls } = fakeFetch([{ body: { status: 'ok' } }]);
    await createApi(fn, 'http://127.0.0.1:8080').health();
    expect(calls[0].url).toBe('http://127.0.0.1:8080/api/health');
    expect(calls[0].init?.body).toBeUndefined();
  });

  it('turns the server error body into an ApiError with code and message', async () => {
    const { fn } = fakeFetch([{ status: 400, body: { error: { code: 'unknown_node', message: "unknown node 'X'" } } }]);
    const error = await createApi(fn).run({ algorithm: 'bfs', source: 'X' }).catch((e) => e);
    expect(error).toBeInstanceOf(ApiError);
    expect(error.status).toBe(400);
    expect(error.code).toBe('unknown_node');
    expect(error.message).toBe("unknown node 'X'");
  });

  it('survives a non-JSON error body', async () => {
    const { fn } = fakeFetch([{ status: 502, text: '<html>Bad gateway</html>' }]);
    const error = await createApi(fn).network().catch((e) => e);
    expect(error).toBeInstanceOf(ApiError);
    expect(error.status).toBe(502);
    expect(error.message).toContain('502');
  });

  it('reports an unreachable server with instructions instead of a raw TypeError', async () => {
    const { fn } = fakeFetch([new TypeError('Failed to fetch')]);
    const error = await createApi(fn).health().catch((e) => e);
    expect(error).toBeInstanceOf(ApiError);
    expect(error.status).toBe(0);
    expect(error.code).toBe('unreachable');
    expect(error.message).toContain('scx_server');
  });

  it('unwraps list endpoints', async () => {
    const { fn } = fakeFetch([{ body: { algorithms: [{ id: 'bfs' }] } }, { body: { results: [{ id: 1 }] } }]);
    const api = createApi(fn);
    expect((await api.algorithms())[0].id).toBe('bfs');
    expect((await api.results())[0].id).toBe(1);
  });
});
