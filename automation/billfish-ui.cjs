'use strict';

// Billfish-owned Qt input over its current-user automation pipe. No desktop input.
const fs = require('node:fs/promises');
const path = require('node:path');
const net = require('node:net');
const { randomUUID } = require('node:crypto');
const { TextDecoder } = require('node:util');

const MAX_REQUEST = 1024 * 1024;
const MAX_RESPONSE = 16 * MAX_REQUEST;
const ACTIONS = new Set(['click', 'type', 'key', 'scroll', 'drag', 'capture', 'invoke']);

class BillfishError extends Error {
  constructor(code, message, details) {
    super(message);
    this.name = 'BillfishError';
    this.code = code;
    this.details = details;
  }
}

function fail(code, message, details) { throw new BillfishError(code, message, details); }
function object(value) { return value !== null && typeof value === 'object' && !Array.isArray(value); }
function sameRoot(left, right) {
  return typeof left === 'string' && path.resolve(left).toLowerCase() === path.resolve(right).toLowerCase();
}

async function connect({ installRoot = path.resolve(__dirname, '..'), timeout = 30000 } = {}) {
  if (typeof installRoot !== 'string' || !Number.isInteger(timeout) || timeout < 1 || timeout > 3600000)
    fail('invalid_arguments', 'installRoot must be a path and timeout must be 1..3600000 milliseconds.');
  installRoot = path.resolve(installRoot);
  let descriptor;
  try {
    if (!process.env.LOCALAPPDATA) throw new Error('LOCALAPPDATA is unavailable.');
    const source = await fs.readFile(path.join(process.env.LOCALAPPDATA, 'Billfish', 'automation', 'host.json'));
    if (source.length > 65536) throw new Error('Descriptor exceeds 64 KiB.');
    descriptor = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(source).replace(/^\uFEFF/, ''));
    if (!object(descriptor) || descriptor.protocol !== 1 || !Number.isInteger(descriptor.pid) ||
        descriptor.pid < 1 || descriptor.pid > 0xffffffff ||
        descriptor.pipe_name !== `BillfishAutomation-v1-${descriptor.pid}` ||
        !sameRoot(descriptor.install_root, installRoot)) throw new Error('Host identity or installation mismatch.');
  } catch (error) {
    fail('online_unavailable', 'No valid Billfish host descriptor for this installation.', { reason: error.message });
  }

  // Keep identity fixed for this connection. Reconnect after restarting Billfish.
  const pipe = `\\\\.\\pipe\\${descriptor.pipe_name}`;
  const snapshots = new WeakMap();
  async function call(operation, args = {}) {
    if (operation !== 'host.info' && operation !== 'ui.snapshot' &&
        !ACTIONS.has(operation.startsWith('ui.') ? operation.slice(3) : ''))
      fail('unknown_operation', 'This client accepts host.info and scoped ui operations only.');
    if (!object(args)) fail('invalid_arguments', 'Arguments must be an object.');
    const id = randomUUID();
    let raw;
    try { raw = Buffer.from(JSON.stringify({ id, operation, arguments: args }) + '\n', 'utf8'); }
    catch (error) { fail('invalid_arguments', 'Arguments must be JSON serializable.', { reason: error.message }); }
    if (raw.length > MAX_REQUEST) fail('invalid_arguments', 'Request exceeds 1 MiB.');
    const limit = ['ui.snapshot', 'ui.capture'].includes(operation) ? MAX_RESPONSE : MAX_REQUEST;
    return new Promise((resolve, reject) => {
      const socket = net.createConnection(pipe);
      let chunks = [], size = 0, settled = false;
      const finish = (error, data) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        socket.destroy();
        if (error) reject(error); else resolve(data);
      };
      const uncertain = (code, message, extra = {}) => finish(new BillfishError(code,
        message + ' Request outcome may be unknown; take a fresh snapshot before retrying input. No automatic retry was made.',
        { request_id: id, job_id: id, ...extra }));
      const timer = setTimeout(() => uncertain('online_timeout', 'Billfish request timed out.'), timeout);
      socket.once('connect', () => socket.write(raw));
      socket.once('error', error => uncertain('online_unavailable', 'Billfish pipe connection failed.', { reason: error.message }));
      socket.once('end', () => { if (!settled) uncertain('online_protocol_error', 'Billfish disconnected before a complete reply.'); });
      socket.on('data', chunk => {
        chunks.push(chunk); size += chunk.length;
        if (size > limit) return uncertain('online_protocol_error', 'Reply exceeds the response limit.', { limit_bytes: limit });
        if (!chunk.includes(10)) return;
        try {
          const frame = Buffer.concat(chunks, size);
          const newline = frame.indexOf(10);
          if (newline !== frame.length - 1) throw new Error('Expected one response frame.');
          const body = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(frame));
          if (!object(body) || body.id !== id || typeof body.ok !== 'boolean') throw new Error('Response identity mismatch.');
          if (body.ok) {
            if (!Object.hasOwn(body, 'data') || Object.hasOwn(body, 'error')) throw new Error('Invalid success envelope.');
            return finish(null, body.data);
          }
          if (!object(body.error) || typeof body.error.code !== 'string' || typeof body.error.message !== 'string')
            throw new Error('Invalid error envelope.');
          finish(new BillfishError(body.error.code, body.error.message,
            { ...(object(body.error.details) ? body.error.details : {}), request_id: id, job_id: id }));
        } catch (error) { uncertain('online_protocol_error', 'Invalid Billfish response.', { reason: error.message }); }
      });
    });
  }

  const host = await call('host.info');
  async function snapshot(options = {}) {
    const state = await call('ui.snapshot', options);
    if (!object(state) || typeof state.snapshot_id !== 'string' || !state.snapshot_id)
      fail('online_protocol_error', 'Snapshot has no usable snapshot_id.');
    snapshots.set(state, state.snapshot_id);
    return state;
  }
  function scope(state, ref) {
    if (!snapshots.has(state) || typeof ref !== 'string' || !ref)
      fail('invalid_arguments', 'Scope requires a snapshot obtained from this client and a nonempty ref.');
    const snapshot_id = snapshots.get(state);
    return Object.freeze({
      snapshot_id, ref,
      call(action, args = {}) {
        if (!ACTIONS.has(action) || !object(args) || Object.hasOwn(args, 'snapshot_id') || Object.hasOwn(args, 'ref'))
          fail('invalid_arguments', 'Use a supported action with arguments; scope supplies snapshot_id and ref.');
        return call('ui.' + action, { ...args, snapshot_id, ref });
      }
    });
  }
  return Object.freeze({ host, installRoot, call, snapshot, scope });
}

module.exports = { connect, BillfishError };
