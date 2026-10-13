// LanguageSourceV1 premises (docs/design/source-recovery/language-source.md).
// Each complete fixture recovers only under --language-source and executes
// like its original source; under the previous numeric+constructors contract
// it stays refused with its exact typed cause chain.
//
// usage: node language_source.mjs ABC_STRUCTURE FIXTURE_DIR
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {copyFileSync, mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

const [structure, fixtures] = process.argv.slice(2);
const scope = ['CaptureCellError=3', 'CaptureError=1', 'ScopeError=3'];
// Complete and Node-equal under --language-source. The standalone es2abc may
// still guard these reads (its elision differs from the DevEco pipeline), so
// the previous contract's outcome is only pinned where `refused` is given;
// language_source_corpus_test pins the unguarded corpus shape.
const cases = [
  {name: 'hoisted', refused: null},
  // An early call writes the binding before its declaration runs: the source
  // and the output both throw ReferenceError (the VM write would complete).
  {name: 'earlywrite', refused: scope},
];
// Handler-covered reads AND writes are checked by family_cells_test using
// successfully imported file-backed phased inputs. Their strict Language
// obligation must reach the normal parent and refuse with
// CaptureCellError::kInput > CaptureError::kScope > ScopeError::kInitialization.
// The old tryread fixture stopped at kImport and did not exercise this gate.
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
  for (const {name, refused} of cases) {
    const {output, record} = exportRecord(name, ['--language-source']);
    assert.equal(record.source_status, 0, name + ' ' + JSON.stringify(record));
    const original = join(directory, name + '.orig.mjs');
    copyFileSync(join(fixtures, name + '.js'), original);
    assert.equal(evaluate(join(output, record.source)), evaluate(original), name);
    if (!refused) continue;
    const previous = exportRecord(name, ['--language-numeric', '--language-constructors']).record;
    assert.equal(previous.source_status, 3, name);
    const causes = previous.source_failure?.causes ?? previous.source_causes;
    assert.deepEqual(causes.map(cause => cause.domain.split('::').at(-1) + '=' + cause.value), refused, name);
  }
} finally {
  rmSync(directory, {recursive: true, force: true});
}
