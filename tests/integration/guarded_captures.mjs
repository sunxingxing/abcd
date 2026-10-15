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
const complete = ['probe', 'twice', 'mutate', 'parentread', 'rhs', 'forward', 'forwardmixed', 'laterblock',
  'recursion', 'initthrow', 'fnowner', 'ownread'];
const refused = {
  childwrite: {domain: 'abcd::source::ScopeError', value: 3},
  forwardwrite: {domain: 'abcd::source::ScopeError', value: 3},
  ownwrite: {domain: 'abcd::source::ScopeError', value: 3},
  // Round 68 proves the earlier allocation dominates this creation. The
  // owner still fails evaluation; no source is published. Round 75 preserves
  // the actual missing evaluation-shape leaf instead of collapsing it.
  branch: [{domain: 'abcd::source::EvaluationError', value: 2},
    {domain: 'abcd::source::EvaluationObligation', value: 0},
    {domain: 'abcd::source::EvaluationShapeError', value: 0}],
};
const runner = `
const show = v => typeof v === 'function' ? 'function' : Array.isArray(v) ? '[' + v.map(show) + ']' : String(v);
globalThis.__probe = f => { try { return 'value:' + show(f()); } catch (e) { return 'throw:' + e.constructor.name; } };
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
    // Each `let X = V;` becomes `X = V;` with `let X;` at the top of its
    // enclosing block (the nearest less-indented line opening a brace).
    const lines = readFileSync(decompiled, 'utf8').split('\n');
    const declaration = /^(\s*)let (\$\w+) = (.*);$/;
    const found = lines.map((line, i) => [i, line.match(declaration)]).filter(([, m]) => m);
    assert.ok(found.length > 0, name + ' declares its cell at the store');
    const inserts = found.map(([i, [, indent, symbol, value]]) => {
      lines[i] = `${indent}${symbol} = ${value};`;
      let owner = i - 1;
      while (owner >= 0 && !(lines[owner].trimEnd().endsWith('{') &&
          lines[owner].length - lines[owner].trimStart().length < indent.length)) --owner;
      return [owner + 1, `${indent}let ${symbol};`];
    });
    for (const [at, line] of inserts.sort((a, b) => b[0] - a[0])) lines.splice(at, 0, line);
    const hoisted = lines.join('\n');
    const mutated = join(directory, name + '.hoisted.mjs');
    writeFileSync(mutated, hoisted);
    assert.notEqual(evaluate(mutated), expected, name + ' mutation must be detected');
  }
  for (const [name, leaf] of Object.entries(refused)) {
    const {record} = exportRecord(name);
    assert.equal(record.source_status, 3, name);
    const causes = record.source_failure?.causes ?? record.source_causes;
    assert.deepEqual(Array.isArray(leaf) ? causes : causes.at(-1), leaf, name + ' ' + JSON.stringify(causes));
    assert.equal(record.source, undefined, name);
  }
} finally {
  rmSync(directory, {recursive: true, force: true});
}
