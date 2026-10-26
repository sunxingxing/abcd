// External class-root demands must be returned before freezing the creator.
// JS is an independent oracle, never an engine input. Compare exception class
// and observable traces, not message/stack or Ark VM execution.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, copyFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const [structure, fixtures] = process.argv.slice(2);
const directory = mkdtempSync(join(tmpdir(), 'abcd-class-deferred-'));
const runner = `
const trace = [];
let escaped;
const mode = process.argv[3];
const observe = f => {
  try { const v = f(); const r = typeof v === 'function' ? 'class' : v; trace.push(['value', r]); return r; }
  catch (e) { trace.push(['throw', e.constructor.name]); return e.constructor.name; }
};
globalThis.__observe = observe;
globalThis.__initialize = c => {
  trace.push(['initialize']); escaped = c.read; observe(c.read);
  if (mode === 'initialize-throw') throw new RangeError();
  return 9;
};
globalThis.__heritage = () => {
  trace.push(['heritage']);
  if (mode === 'heritage-throw') throw new SyntaxError();
  return class {};
};
globalThis.__reenter = f => { trace.push(['reenter']); observe(f); };
try {
  const m = await import(process.argv[2]);
  trace.push(['loaded']);
  if (m.probe) observe(m.probe);
} catch (e) { trace.push(['module-throw', e.constructor.name]); }
if (escaped) { trace.push(['escaped']); observe(escaped); }
console.log(JSON.stringify(trace));
`;
try {
  writeFileSync(join(directory, 'runner.mjs'), runner);
  const evaluate = (file, mode) => {
    const run = spawnSync(process.execPath, [join(directory, 'runner.mjs'), file, mode], {encoding: 'utf8'});
    assert.equal(run.status, 0, run.stderr);
    return run.stdout.trim();
  };
  for (const [flag, args] of [
    ['previous', ['--language-numeric', '--language-constructors']], ['source', ['--language-source']],
  ]) {
    for (const name of ['self', 'late', 'multiple', 'phased', 'heritage', 'reentrant', 'static_early', 'loop']) {
      const out = join(directory, flag + '-' + name);
      const run = spawnSync(structure, [join(fixtures, name + '.abc'), out, '--value-source', ...args], {encoding: 'utf8'});
      assert.equal(run.status, 0, run.stdout + run.stderr);
      const records = JSON.parse(readFileSync(join(out, 'manifest.json'))).records.filter(r => r.source_status !== 2);
      assert.equal(records.length, 1);
      const record = records[0];
      // Timing and nested-class activation are not widened by this increment.
      if (name === 'static_early' || name === 'loop') {
        assert.equal(record.source_status, 3, JSON.stringify(record));
        assert.equal(record.source, undefined);
        if (name === 'loop') {
          // Phased discovery now reaches the real capture requirement. The
          // loop still refuses; no activation or TDZ requirement is relaxed.
          assert.deepEqual(record.source_failure.causes, [
            {domain: 'abcd::source::FamilyFileError', value: 1},
            {domain: 'abcd::source::FamilyCreationCheck', value: 16},
          ]);
          const creation = record.source_failure.family_creation;
          assert.equal(creation.reason, 16); assert.equal(creation.kind, 2);
          assert.deepEqual(creation.location, {method: 395, operation: 16, block: 32, pc: 38, working_pc: 38});
          assert.equal(creation.root, 423); assert.equal(creation.occurrence, 1);
          assert.deepEqual(creation.descendant_source, {method: 423, operation: 3, block: 0, pc: 6, working_pc: 6});
        }
        else { assert.equal(record.source_failure_domain, 'record_program'); assert.equal(record.source_error, 7); }
        continue;
      }
      assert.equal(record.source_status, 0, JSON.stringify(record));
      const original = join(directory, name + '.original.mjs');
      copyFileSync(join(fixtures, name + '.js'), original);
      const source = join(out, record.source);
      const text = readFileSync(source, 'utf8');
      for (const mode of ['normal', 'initialize-throw', 'heritage-throw']) {
        const expected = evaluate(original, mode);
        assert.equal(evaluate(source, mode), expected, name + '/' + flag + '/' + mode);
        if (name === 'late' || name === 'multiple' || name === 'phased') {
          assert.ok(expected.includes('ReferenceError'), expected);
          // Same storage requested by two class occurrences: one declaration.
          const matches = [...text.matchAll(/^let (\$\w+) = __initialize\(/gm)];
          assert.equal(matches.length, 1, text);
          const symbol = matches[0][1];
          const changed = `let ${symbol};\n` + text.replace(`let ${symbol} =`, `${symbol} =`);
          const mutation = join(directory, 'hoisted.mjs'); writeFileSync(mutation, changed);
          assert.notEqual(evaluate(mutation, mode), expected, 'hoisting loses TDZ: ' + name);
        }
      }
      if (name === 'self' || name === 'heritage' || name === 'reentrant') {
        // Do not conflate the inner name with a different outer/storage binding.
        const changed = text.replace(/return \$local\d+;/g, 'return undefined;');
        assert.notEqual(changed, text, 'self cell must have an independently bound alias');
        const mutation = join(directory, 'wrong-self.mjs'); writeFileSync(mutation, changed);
        assert.notEqual(evaluate(mutation, 'normal'), evaluate(original, 'normal'), name + ' self alias mutation');
      }
    }
  }
} finally { rmSync(directory, {recursive: true, force: true}); }
