// SPDX-License-Identifier: BSD-3-Clause
// Run: node --experimental-default-type=module --test tests/web_storage_tests.mjs

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { StorageCoordinator } from '../web/storage.js';

function fixture(t) {
  const storage = new StorageCoordinator();
  const callbacks = [];
  storage.fs = {
    syncfs(populate, callback) {
      assert.equal(populate, false);
      callbacks.push(callback);
    }
  };
  t.after(() => clearTimeout(storage.debounceTimer));
  return { storage, callbacks };
}

// Let promise continuations run without waiting for the debounce timer.
const nextTurn = () => new Promise((resolve) => setImmediate(resolve));

test('overlapping flushes await every pending generation', async (t) => {
  const { storage, callbacks } = fixture(t);
  storage.notifyMutation();
  let completed = false;
  const first = storage.flush();
  first.then(() => { completed = true; });
  await nextTurn();
  assert.equal(callbacks.length, 1);

  storage.notifyMutation();
  const second = storage.flush();
  assert.equal(second, first);
  await nextTurn();
  assert.equal(completed, false);
  assert.equal(callbacks.length, 1);

  callbacks.shift()(null);
  await nextTurn();
  assert.equal(completed, false);
  assert.equal(storage.syncedGeneration, 1);
  assert.equal(callbacks.length, 1);

  storage.notifyMutation();
  callbacks.shift()(null);
  await nextTurn();
  assert.equal(completed, false);
  assert.equal(callbacks.length, 1);
  callbacks.shift()(null);

  assert.deepEqual(await Promise.all([first, second]), [true, true]);
  assert.equal(storage.syncedGeneration, 3);
  assert.equal(storage.state, 'saved');
  assert.equal(storage.isSyncing, false);
  assert.equal(storage.debounceTimer, null);
  assert.equal(await storage.flush(), true);
  assert.equal(callbacks.length, 0);
});

test('a failed follow-up sync fails all waiters and can be retried', async (t) => {
  const { storage, callbacks } = fixture(t);
  t.mock.method(console, 'error', () => {});
  storage.notifyMutation();
  const first = storage.flush();
  await nextTurn();
  storage.notifyMutation();
  const second = storage.flush();
  callbacks.shift()(null);
  await nextTurn();
  callbacks.shift()(new Error('disk full'));
  assert.deepEqual(await Promise.all([first, second]), [false, false]);
  assert.equal(storage.syncedGeneration, 1);
  assert.equal(storage.pendingGeneration, 2);
  assert.equal(storage.state, 'error');
  assert.equal(storage.lastError, 'disk full');
  assert.equal(storage.isSyncing, false);

  const retry = storage.flush();
  await nextTurn();
  callbacks.shift()(null);
  assert.equal(await retry, true);
  assert.equal(storage.syncedGeneration, 2);
  assert.equal(storage.lastError, null);
});

test('a synchronous syncfs exception leaves pending changes retryable', async (t) => {
  const { storage } = fixture(t);
  t.mock.method(console, 'error', () => {});
  storage.fs.syncfs = () => { throw new Error('unavailable'); };
  storage.notifyMutation();
  assert.equal(await storage.flush(), false);
  assert.equal(storage.syncedGeneration, 0);
  assert.equal(storage.state, 'error');
  assert.equal(storage.isSyncing, false);
  storage.fs.syncfs = (_populate, callback) => callback(null);
  assert.equal(await storage.flush(), true);
  assert.equal(storage.syncedGeneration, 1);
});

test('mutations from state listeners are included in the active flush', async (t) => {
  const { storage, callbacks } = fixture(t);
  let listenerFlush;
  storage.onStateChange(({ state }) => {
    if (state === 'saved' && storage.pendingGeneration === 1) {
      storage.notifyMutation();
      listenerFlush = storage.flush();
    }
  });
  storage.notifyMutation();
  let completed = false;
  const flush = storage.flush();
  flush.then(() => { completed = true; });
  await nextTurn();
  callbacks.shift()(null);
  await nextTurn();
  assert.equal(listenerFlush, flush);
  assert.equal(completed, false);
  assert.equal(callbacks.length, 1);
  callbacks.shift()(null);
  assert.equal(await flush, true);
  assert.equal(storage.syncedGeneration, 2);
});

function projectFixture(t) {
  const { storage } = fixture(t);
  const files = new Map([
    ['/data/.current', 'active'],
    ['/data/projects/active/ptsav.dat', new Uint8Array([1])],
    ['/data/projects/other/ptsav.dat', new Uint8Array([2])]
  ]);
  const directories = new Set(['/data/projects/active', '/data/projects/other']);
  Object.assign(storage.fs, {
    readFile(path) {
      if (!files.has(path)) throw new Error('File not found');
      return files.get(path);
    },
    analyzePath: (path) => ({ exists: directories.has(path) || files.has(path) }),
    readdir: () => ['.', '..', 'ptsav.dat'],
    stat: () => ({ mode: 0 }),
    isDir: () => false,
    unlink: (path) => files.delete(path),
    rmdir: (path) => directories.delete(path)
  });
  return { storage, files, directories };
}

test('deleting the current project leaves its files and marker intact', (t) => {
  const { storage, files, directories } = projectFixture(t);
  assert.throws(() => storage.deleteProject('active'), /Switch projects first/);
  assert.equal(directories.has('/data/projects/active'), true);
  assert.equal(files.has('/data/projects/active/ptsav.dat'), true);
  assert.equal(files.get('/data/.current'), 'active');
  assert.equal(storage.pendingGeneration, 0);
});

test('deletion rechecks the current project after a project switch', (t) => {
  const { storage, files, directories } = projectFixture(t);
  assert.equal(storage.getCurrentProjectName(), 'active');
  files.set('/data/.current', 'other');
  assert.throws(() => storage.deleteProject('other'), /Switch projects first/);
  storage.deleteProject('active');
  assert.equal(directories.has('/data/projects/active'), false);
  assert.equal(files.has('/data/projects/active/ptsav.dat'), false);
  assert.equal(files.has('/data/projects/other/ptsav.dat'), true);
  assert.equal(files.get('/data/.current'), 'other');
  assert.equal(storage.pendingGeneration, 1);
});

test('deletion is blocked when the current project cannot be read', (t) => {
  const { storage, files, directories } = projectFixture(t);
  files.delete('/data/.current');
  assert.throws(() => storage.deleteProject('active'), /Cannot determine/);
  files.set('/data/.current', '');
  assert.throws(() => storage.deleteProject('other'), /Cannot determine/);
  assert.equal(directories.size, 2);
  assert.equal(storage.pendingGeneration, 0);
});
