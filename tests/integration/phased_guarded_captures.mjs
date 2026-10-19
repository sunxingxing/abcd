// Real es2abc inputs; original source is an independent Node oracle. Exception
// constructor/control/value/log traces are compared; message and stack are not.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, copyFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const [structure, fixtures] = process.argv.slice(2);
const dir = mkdtempSync(join(tmpdir(), 'abcd-phased-guarded-'));
try {
  const runner = join(dir, 'run.mjs');
  writeFileSync(runner, `globalThis.trace = [];
const m = await import(process.argv[2]);
console.log(JSON.stringify({early:m.early,late:m.late,trace:globalThis.trace}));`);
  const run = path => spawnSync(process.execPath, [runner, path], {encoding:'utf8'});
  for (const [flags, label] of [[['--language-numeric', '--language-constructors'], 'old'], [['--language-source'], 'source']]) {
    for (const name of ['read', 'forward', 'write']) {
      const output = join(dir, label + '-' + name);
      const exported = spawnSync(structure, [join(fixtures, name + '.abc'), output, '--value-source', ...flags], {encoding:'utf8'});
      assert.equal(exported.status, 0, exported.stderr);
      const records = JSON.parse(readFileSync(join(output, 'manifest.json'))).records.filter(r => r.source_status !== 2);
      assert.equal(records.length, 1);
      const record = records[0];
      if (name === 'write') {
        assert.equal(record.source_status, 3);
        assert.deepEqual(record.source_failure.causes.map(c => [c.domain, c.value]), [
          ['abcd::source::CaptureCellError', 3], ['abcd::source::CaptureError', 1], ['abcd::source::ScopeError', 3]]);
        assert.equal(record.source_failure.capture.requested, 0); // Language, never Guarded/SourceAccess.
        assert.ok(Number.isInteger(record.source_failure.capture.pc));
        continue;
      }
      assert.equal(record.source_status, 0, JSON.stringify(record));
      const original = join(dir, label + '-' + name + '.mjs');
      copyFileSync(join(fixtures, name + '.js'), original);
      const expected = run(original); assert.equal(expected.status, 0, expected.stderr);
      assert.ok(expected.stdout.includes('ReferenceError'));
      const source = join(output, record.source);
      const actual = run(source); assert.equal(actual.status, 0, actual.stderr);
      assert.equal(actual.stdout, expected.stdout, label + '/' + name);
      const text = readFileSync(source, 'utf8');
      const declaration = /^let (\$\w+) = 42;$/m.exec(text);
      assert.ok(declaration, 'deferred cell declaration retained');
      const cell = declaration[1];
      const mutations = [
        `let ${cell};\n` + text.replace(declaration[0], `${cell} = 42;`),
        // Remove the native checked read, preserving the declaration/store.
        text.replaceAll(`= ${cell};`, '= void 0;').replaceAll(`return ${cell};`, 'return void 0;'),
      ];
      if (name === 'read') {
        assert.ok(/\$exception\d+ = 7;/.test(text), 'catch state staging is exercised');
        mutations.push(text.replace(/(\$exception\d+ = )7(;)/, '$19$2'));
      }
      for (const [i, mutant] of mutations.entries()) {
        assert.notEqual(mutant, text);
        const file = join(dir, `${label}-${name}-mutant-${i}.mjs`); writeFileSync(file, mutant);
        const result = run(file);
        assert.ok(result.status !== 0 || result.stdout !== expected.stdout, 'mutation must be detected: ' + name + '/' + i);
      }
    }
  }
} finally { rmSync(dir, {recursive:true, force:true}); }
