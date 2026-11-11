// SPDX-License-Identifier: BSD-3-Clause
// Factory content seeding tests: node web/tests/storage-seeding.test.mjs

import { MemFS } from './memfs.mjs';
import { storage } from '../storage.js';

function factoryFixture() {
  const fs = new MemFS();
  const f = '/defaults/factory';
  fs.writeFile(`${f}/default-current.txt`, 'oneCycAc');
  fs.writeFile(`${f}/projects/oneCycAc/ptsav.dat`, 'AAA');
  fs.writeFile(`${f}/projects/oneCycAc/samples/AKWF_0906.wav`, 'WAV1');
  fs.writeFile(`${f}/projects/dark-fog/ptsav.dat`, 'BBB');
  fs.writeFile(`${f}/projects/dark-fog/samples/x.wav`, 'WAV2');
  fs.writeFile(`${f}/themes/monokai1.ptt`, 'T1');
  fs.writeFile(`${f}/samples/drums/kick.wav`, 'K');
  fs.mkdir('/data');
  return fs;
}
let pass = 0, fail = 0;
function check(label, actual, expected) {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);
  ok ? pass++ : fail++;
  console.log(`${ok ? 'PASS' : 'FAIL'} ${label}${ok ? '' : ` (got ${JSON.stringify(actual)}, want ${JSON.stringify(expected)})`}`);
}
function cur(fs) { try { return fs.readFile('/data/.current', { encoding: 'utf8' }); } catch { return null; } }

// 1. fresh storage
let fs = factoryFixture();
storage.fs = fs; storage.mutationCount = 0; storage.notifyMutation = () => { storage.mutationCount++; };
check('fresh: seeded', storage.seedFactoryContent(), true);
check('fresh: both projects installed', fs.readdir('/data/projects').sort(), ['dark-fog', 'oneCycAc']);
check('fresh: project files copied', fs.list().filter((p) => p.startsWith('/data/projects')), [
  '/data/projects/dark-fog/ptsav.dat', '/data/projects/dark-fog/samples/x.wav',
  '/data/projects/oneCycAc/ptsav.dat', '/data/projects/oneCycAc/samples/AKWF_0906.wav']);
check('fresh: .current from default-current.txt', cur(fs), 'oneCycAc');
check('fresh: themes + samples merged', fs.list().filter((p) => p.startsWith('/data/themes') || p.startsWith('/data/samples')),
  ['/data/samples/drums/kick.wav', '/data/themes/monokai1.ptt']);
check('fresh: idempotent second run', storage.seedFactoryContent(), false);

// 2. existing user project keeps its .current
fs = factoryFixture();
storage.fs = fs;
fs.writeFile('/data/projects/my-song/ptsav.dat', 'ZZZ');
fs.writeFile('/data/.current', 'my-song');
storage.seedFactoryContent();
check('user project preserved', fs.readdir('/data/projects').sort(), ['dark-fog', 'my-song', 'oneCycAc']);
check('user .current untouched', cur(fs), 'my-song');

// 3. deleted factory project returns, but .current stays on user project
fs.writeFile('/data/.current', 'oneCycAc');
check('delete current project throws', (() => { try { storage.deleteProject('oneCycAc'); return 'no-throw'; } catch (e) { return 'threw'; } })(), 'threw');
storage.deleteProject('dark-fog');
check('delete non-current works', fs.analyzePath('/data/projects/dark-fog').exists, false);
storage.seedFactoryContent();
check('deleted factory project reinstalled', fs.analyzePath('/data/projects/dark-fog/ptsav.dat').exists, true);
check('.current still oneCycAc', cur(fs), 'oneCycAc');

// 4. empty .current gets seeded
fs = factoryFixture();
storage.fs = fs;
fs.writeFile('/data/projects/oneCycAc/ptsav.dat', 'AAA');
fs.writeFile('/data/.current', '');
check('empty .current replaced', storage.seedFactoryContent() && cur(fs), 'oneCycAc');

// 5. missing default project in factory -> no .current written, other content still seeds
fs = factoryFixture();
storage.fs = fs;
fs.unlink('/defaults/factory/projects/oneCycAc/ptsav.dat');
fs.unlink('/defaults/factory/projects/oneCycAc/samples/AKWF_0906.wav');
storage.seedFactoryContent();
check('broken default project: no .current', cur(fs), null);
check('broken default project: dark-fog still installed', fs.analyzePath('/data/projects/dark-fog/ptsav.dat').exists, true);

// 6. project missing ptsav.dat is skipped, not created half-written
fs = factoryFixture();
storage.fs = fs;
fs.writeFile('/defaults/factory/projects/broken/README.txt', 'no marker');
storage.seedFactoryContent();
check('project without ptsav.dat skipped', fs.analyzePath('/data/projects/broken').exists, false);

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
