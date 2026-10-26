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
  const implicit = statuses([]), explicit = statuses(['--language-source']);
  for (const page of pages) {
    assert.equal(implicit.get(page).source_status, 0, page + ' ' + JSON.stringify(implicit.get(page)));
    assert.deepEqual(implicit.get(page), explicit.get(page), page);
  }

} finally {
  rmSync(directory, {recursive: true, force: true});
}
