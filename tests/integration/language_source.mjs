// LanguageSourceV1 premises (docs/design/source-recovery/language-source.md).
// Each fixture executes like its original source under both the default
// contract and its explicit --language-source alias.
//
// usage: node language_source.mjs ABC_STRUCTURE FIXTURE_DIR
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {copyFileSync, mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

const [structure, fixtures] = process.argv.slice(2);
const cases = [{name: 'hoisted'}, {name: 'earlywrite'}];
const directory = mkdtempSync(join(tmpdir(), 'abcd-language-source-'));
try {
  const exportRecord = (name, flags) => {
    const output = join(directory, `out-${name}-${flags.length}`);
    const run = spawnSync(structure, [join(fixtures, name + '.abc'), output, '--value-source', ...flags],
        {encoding: 'utf8'});
    assert.equal(run.status, 0, run.stderr);
    const manifest = JSON.parse(readFileSync(join(output, 'manifest.json'), 'utf8'));
    const records = manifest.records.filter(record => record.source_status !== 2);
    assert.equal(records.length, 1, name);
    return {output, record: records[0]};
  };
  const evaluate = file => {
    const run = spawnSync(process.execPath, [join(fixtures, 'harness.mjs'), file], {encoding: 'utf8'});
    assert.equal(run.status, 0, run.stderr);
    return run.stdout.trim();
  };
  for (const {name} of cases) {
    const {output, record} = exportRecord(name, ['--language-source']);
    assert.equal(record.source_status, 0, name + ' ' + JSON.stringify(record));
    const original = join(directory, name + '.orig.mjs');
    copyFileSync(join(fixtures, name + '.js'), original);
    assert.equal(evaluate(join(output, record.source)), evaluate(original), name);
    const defaultExport = exportRecord(name, []);
    assert.equal(defaultExport.record.source_status, 0, name);
    assert.equal(readFileSync(join(defaultExport.output, defaultExport.record.source), 'utf8'),
                 readFileSync(join(output, record.source), 'utf8'));

  }
} finally {
  rmSync(directory, {recursive: true, force: true});
}
