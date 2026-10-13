// Corpus shape of the source TDZ premise (language-source.md §1), from the
// exploration cohort (DevEco build). Hoisted top-level functions read later
// module-scope bindings, and a callback capturing the environment escapes to a
// host call before the store. Round 38's guarded capture-read proof completes
// the pages whose early reads carry a named hole check under the previous
// contract too (FindAndListenAudioInputDevice, InputDeviceRoutingSwitching,
// ListenDeviceByAudioSession). OutputDeviceChangePause still needs the premise
// and keeps the initialization cause chain; FindAndListenAudioOutputDevice
// passes initialization but stops at a later call-plan cause. With
// --language-source all five pages complete.
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
  const strict = statuses(['--language-numeric', '--language-constructors']);
  for (const page of ['FindAndListenAudioInputDevice', 'InputDeviceRoutingSwitching', 'ListenDeviceByAudioSession'])
    assert.equal(strict.get(page).source_status, 0, page + ' ' + JSON.stringify(strict.get(page)));
  refused('OutputDeviceChangePause', strict.get('OutputDeviceChangePause'));
  assert.equal(strict.get('FindAndListenAudioOutputDevice').source_status, 3);
  for (const [page, record] of statuses(['--language-source']))
    assert.equal(record.source_status, 0, page + ' ' + JSON.stringify(record));
} finally {
  rmSync(directory, {recursive: true, force: true});
}
