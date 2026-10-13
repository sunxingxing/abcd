// Corpus shape of the source TDZ premise (language-source.md §1), from the
// exploration cohort (DevEco build; the standalone es2abc guards these reads).
// Hoisted top-level functions read a later module-scope binding without a hole
// check, while a callback capturing the environment escapes to a host call
// before the store. The previous contract refuses all five pages with the
// initialization cause chain; --language-source completes four after Round A's
// forwarding fix. InputDeviceRoutingSwitching contains an async captured access
// with a user try (method 16458), which remains strict until Round B's typed
// source-TDZ exception projection (language-source.md).
//
// usage: node language_source_corpus.mjs ABC_STRUCTURE INPUT.abc
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

const [structure, input] = process.argv.slice(2);
const pages = ['FindAndListenAudioInputDevice', 'FindAndListenAudioOutputDevice', 'InputDeviceRoutingSwitching',
  'ListenDeviceByAudioSession', 'OutputDeviceChangePause'];
const directory = mkdtempSync(join(tmpdir(), 'abcd-language-source-corpus-'));
try {
  const statuses = flags => {
    const output = join(directory, 'out-' + flags.length);
    const run = spawnSync(structure, [input, output, '--value-source', ...flags], {encoding: 'utf8'});
    assert.equal(run.status, 0, run.stderr);
    const manifest = JSON.parse(readFileSync(join(output, 'manifest.json'), 'utf8'));
    const found = new Map();
    for (const record of manifest.records)
      for (const page of pages)
        if ((record.planned_source ?? record.source ?? '').endsWith(`/pages/${page}.mjs`)) found.set(page, record);
    assert.equal(found.size, pages.length);
    return found;
  };
  const refused = (page, record) => {
    assert.equal(record.source_status, 3, page + ' ' + JSON.stringify(record));
    const causes = record.source_failure?.causes ?? record.source_causes;
    assert.deepEqual(causes.map(cause => cause.domain.split('::').at(-1) + '=' + cause.value),
        ['CaptureCellError=3', 'CaptureError=1', 'ScopeError=3'], page);
  };
  for (const [page, record] of statuses(['--language-numeric', '--language-constructors']))
    refused(page, record);
  for (const [page, record] of statuses(['--language-source'])) {
    if (page === 'InputDeviceRoutingSwitching') refused(page, record);
    else assert.equal(record.source_status, 0, page + ' ' + JSON.stringify(record));
  }
} finally {
  rmSync(directory, {recursive: true, force: true});
}
