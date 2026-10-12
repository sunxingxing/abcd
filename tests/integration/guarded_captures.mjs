// Guarded captures before initialization (docs/design/source-recovery/guarded-captures.md).
// A child whose capture reads are TDZ-guarded may be created before its cell's
// first store; the owner must then declare the binding exactly at that store.
// Each complete fixture is decompiled and differentially executed against its
// original source under the same probe. A mutation that hoists the declaration
// (`let x;` up front, `x = v` at the store) must change the observable result,
// proving the comparison detects a lost TDZ. Refusals keep their typed causes.
//
// usage: node guarded_captures.mjs ABC_STRUCTURE FIXTURE_DIR
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {copyFileSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

const [structure, fixtures] = process.argv.slice(2);
const complete = ['probe', 'twice', 'mutate', 'parentread', 'rhs'];
const refused = {
  childwrite: {domain: 'abcd::source::ScopeError', value: 3},
  branch: {domain: 'abcd::source::ScopeError', value: 3},
};
const runner = `
globalThis.__probe = f => { try { return 'value:' + f(); } catch (e) { return 'throw:' + e.constructor.name; } };
const m = await import(process.argv[2]);
console.log(JSON.stringify(Object.fromEntries(Object.keys(m).sort().map(k =>
  [k, typeof m[k] === 'function' ? globalThis.__probe(m[k]) : m[k]]))));
`;
const directory = mkdtempSync(join(tmpdir(), 'abcd-guarded-captures-'));
try {
  writeFileSync(join(directory, 'runner.mjs'), runner);
  const evaluate = file => {
    const run = spawnSync(process.execPath, [join(directory, 'runner.mjs'), file], {encoding: 'utf8'});
    assert.equal(run.status, 0, run.stderr);
    return run.stdout.trim();
  };
  const exportRecord = name => {
    const output = join(directory, 'out-' + name);
    const run = spawnSync(structure, [join(fixtures, name + '.abc'), output, '--value-source',
      '--language-numeric', '--language-constructors'], {encoding: 'utf8'});
    assert.equal(run.status, 0, run.stderr);
    const manifest = JSON.parse(readFileSync(join(output, 'manifest.json'), 'utf8'));
    const records = manifest.records.filter(record => record.source_status !== 2);
    assert.equal(records.length, 1, name);
    return {output, record: records[0]};
  };
  for (const name of complete) {
    const {output, record} = exportRecord(name);
    assert.equal(record.source_status, 0, name + ' ' + JSON.stringify(record));
    const original = join(directory, name + '.orig.mjs');
    copyFileSync(join(fixtures, name + '.js'), original);
    const decompiled = join(output, record.source);
    const expected = evaluate(original);
    assert.ok(expected.includes('throw:ReferenceError'), name + ' exercises TDZ: ' + expected);
    assert.equal(evaluate(decompiled), expected, name);
    // Hoisting any deferred `let x = v` back to an early `let x;` must be observable.
    const text = readFileSync(decompiled, 'utf8');
    const deferred = [...text.matchAll(/^let (\$local\d+) = (.*);$/gm)];
    assert.ok(deferred.length > 0, name + ' declares its cell at the store');
    let hoisted = text;
    for (const [line, symbol, value] of deferred)
      hoisted = `let ${symbol};\n` + hoisted.replace(line, `${symbol} = ${value};`);
    const mutated = join(directory, name + '.hoisted.mjs');
    writeFileSync(mutated, hoisted);
    assert.notEqual(evaluate(mutated), expected, name + ' mutation must be detected');
  }
  for (const [name, leaf] of Object.entries(refused)) {
    const {record} = exportRecord(name);
    assert.equal(record.source_status, 3, name);
    const causes = record.source_failure?.causes ?? record.source_causes;
    assert.deepEqual(causes.at(-1), leaf, name + ' ' + JSON.stringify(causes));
    assert.equal(record.source, undefined, name);
  }
} finally {
  rmSync(directory, {recursive: true, force: true});
}
