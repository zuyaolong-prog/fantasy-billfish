'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const net = require('node:net');
const { connect } = require('../billfish-ui.cjs');

test('client scopes snapshot references and never retries failed input', { skip: process.platform !== 'win32' }, async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'billfish-ui-client-'));
  const previousLocal = process.env.LOCALAPPDATA;
  process.env.LOCALAPPDATA = directory;
  const pid = process.pid;
  const pipeName = `BillfishAutomation-v1-${pid}`;
  const requests = [];
  let mode = 'normal';
  const server = net.createServer(socket => {
    let input = '';
    socket.on('data', chunk => {
      input += chunk;
      if (!input.endsWith('\n')) return;
      const request = JSON.parse(input);
      requests.push(request);
      if (mode === 'disconnect') return socket.end();
      if (mode === 'wrong-id') return socket.end(JSON.stringify({ id: 'wrong', ok: true, data: {} }) + '\n');
      if (mode === 'host-error') return socket.end(JSON.stringify({ id: request.id, ok: false,
        error: { code: 'stale_snapshot', message: 'Take another snapshot.' } }) + '\n');
      const data = request.operation === 'host.info' ? { ready: true } :
        request.operation === 'ui.snapshot' ? { snapshot_id: 'snapshot-1', nodes: [{ ref: 'w1' }] } :
        request.operation === 'ui.capture' ? { mime_type: 'image/png', base64: 'x'.repeat(1024 * 1024 + 1), width: 1, height: 1 } :
        { accepted: true, queued: true };
      socket.end(JSON.stringify({ id: request.id, ok: true, data }) + '\n');
    });
  });
  try {
    await fs.mkdir(path.join(directory, 'Billfish', 'automation'), { recursive: true });
    await fs.writeFile(path.join(directory, 'Billfish', 'automation', 'host.json'), JSON.stringify({
      protocol: 1, pid, pipe_name: pipeName, install_root: directory }));
    await new Promise((resolve, reject) => { server.once('error', reject); server.listen(`\\\\.\\pipe\\${pipeName}`, resolve); });
    const client = await connect({ installRoot: directory, timeout: 5000 });
    assert.equal(client.host.ready, true);
    const state = await client.snapshot();
    state.snapshot_id = 'caller-mutated';
    const target = client.scope(state, 'w1');
    assert.equal(target.snapshot_id, 'snapshot-1');
    assert.deepEqual(await target.call('click', { count: 2 }), { accepted: true, queued: true });
    assert.deepEqual(requests.at(-1).arguments, { count: 2, snapshot_id: 'snapshot-1', ref: 'w1' });
    assert.throws(() => client.scope({ snapshot_id: 'snapshot-1' }, 'w1'), { code: 'invalid_arguments' });
    assert.throws(() => target.call('click', { ref: 'different' }), { code: 'invalid_arguments' });
    await assert.rejects(client.call('items.update', {}), { code: 'unknown_operation' });
    assert.equal((await target.call('capture')).base64.length, 1024 * 1024 + 1);
    for (const [nextMode, code] of [['host-error', 'stale_snapshot'], ['disconnect', 'online_protocol_error'], ['wrong-id', 'online_protocol_error']]) {
      mode = nextMode;
      const before = requests.length;
      await assert.rejects(target.call('click'), { code });
      assert.equal(requests.length, before + 1);
    }
    const before = requests.length;
    await assert.rejects(connect({ installRoot: path.join(directory, 'another') }), { code: 'online_unavailable' });
    assert.equal(requests.length, before);
  } finally {
    await new Promise(resolve => server.close(resolve));
    if (previousLocal === undefined) delete process.env.LOCALAPPDATA;
    else process.env.LOCALAPPDATA = previousLocal;
    await fs.rm(directory, { recursive: true, force: true });
  }
});
