// Real compiler input versus independent source execution; no platform mocks.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, copyFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const [structure, fixture] = process.argv.slice(2);
const dir = mkdtempSync(join(tmpdir(), 'abcd-terminal-regions-'));
try {
  const runner = join(dir, 'run.mjs');
  writeFileSync(runner, `
const m = await import(process.argv[2]);
const runs = [];
for (const name of ['early', 'protectedEarly', 'independent']) {
  for (const outer of [false, true]) for (const stop of [false, true]) for (const choice of [false, true]) {
    for (const fail of ['', ...['enter', 'left', 'right', 'terminal', 'continuing', 'tail', 'caught'].flatMap(key => [key, 'get:' + key])]) {
      const trace = [];
      const hooks = {};
      for (const key of ['enter', 'left', 'right', 'terminal', 'continuing', 'tail', 'caught']) {
        Object.defineProperty(hooks, key, {get() {
          trace.push('get:' + key);
          if (fail === 'get:' + key) throw new TypeError(key);
          return function(value) {
            if (this !== hooks) throw new Error('receiver');
            trace.push([key, value]);
            if (key === fail) throw new RangeError(key);
          };
        }});
      }
      let result;
      try {
        result = {returned: name === 'independent' ? m[name](outer, choice, hooks) : m[name](outer, stop, choice, hooks)};
      } catch (e) { result = {thrown:e.constructor.name}; }
      finally { trace.push('caller-finally'); }
      runs.push({name, outer, stop, choice, fail, result, trace});
    }
  }
}
console.log(JSON.stringify(runs));
`);
  const run = file => spawnSync(process.execPath, [runner, file], {encoding:'utf8'});
  const original = join(dir, 'original.mjs'); copyFileSync(join(fixture, 'terminal.js'), original);
  const expected = run(original); assert.equal(expected.status, 0, expected.stderr);
  for (const [label, flags] of [['old', ['--language-numeric', '--language-constructors']], ['source', ['--language-source']]]) {
    const output = join(dir, label);
    const exported = spawnSync(structure, [join(fixture, 'terminal.abc'), output, '--value-source', ...flags], {encoding:'utf8'});
    assert.equal(exported.status, 0, exported.stderr);
    const records = JSON.parse(readFileSync(join(output, 'manifest.json'))).records.filter(r => r.source_status !== 2);
    assert.equal(records.length, 1); const record = records[0];
    assert.equal(record.source_status, 0, JSON.stringify(record));
    const source = join(output, record.source);
    const actual = run(source); assert.equal(actual.status, 0, actual.stderr);
    assert.equal(actual.stdout, expected.stdout, label);
    const text = readFileSync(source, 'utf8');
    const mutations = [
      text.replaceAll('return "early";', 'void 0;'), // Abrupt arm must not reach the continuing suffix.
      text.replace(/(\$\w+\.terminal\(\);)/g, '$1 $1'),
      text.replaceAll('.left();', '.right();'), // Taken/fallthrough and internal effects.
      text.replace(/\.caught\(\$\w+\.name\)/g, '.caught("wrong")'), // Exception correspondence.
    ];
    for (const [index, changed] of mutations.entries()) {
      assert.notEqual(changed, text, 'mutation must apply');
      const mutant = join(dir, label + '-' + index + '.mjs'); writeFileSync(mutant, changed);
      const result = run(mutant);
      assert.ok(result.status !== 0 || result.stdout !== expected.stdout, 'undetected mutation: ' + index);
    }
  }
} finally { rmSync(dir, {recursive:true, force:true}); }
