import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';

const [structure, abc, source, rejected] = process.argv.slice(2);
const run = (exe, args) => {
  const r = spawnSync(exe, args, {encoding: 'utf8', timeout: 120000, maxBuffer: 32 * 1024 * 1024});
  assert.ifError(r.error); assert.equal(r.signal, null); assert.equal(r.status, 0, r.stderr);
  return r.stdout;
};
const oracle = await import(`data:text/javascript;base64,${Buffer.from(readFileSync(source)).toString('base64')}`);
const directory = mkdtempSync(join(tmpdir(), 'abcd-native-for-in-'));
const objects = [
  () => ({}), () => null, () => undefined,
  () => ({b: 1, 10: 2, 2: 3, '': 4, a: 5}),
  () => Object.assign(Object.create({inherited: 1, shadow: 2}), {shadow: 3, own: 4}),
  log => Object.defineProperties({}, {
    visible: {enumerable: true, get() { log.push(['getter']); return 42; }},
    hidden: {value: 2},
  }),
  log => Object.defineProperties({}, {
    first: {value: 1, enumerable: true},
    throwing: {enumerable: true, get() { log.push(['getter-throw']); throw new RangeError(); }},
  }),
];
function trace(fn, create, mode, throwVisit, deletion = false) {
  const log = [], object = create(log);
  try {
    log.push(['return', fn(object, (...args) => {
      log.push(['visit', ...args]);
      if (deletion && object) delete object.a;
      if (throwVisit) throw new TypeError();
    }, mode)]);
  } catch (error) { log.push(['throw', error instanceof Error ? error.constructor.name : error]); }
  return log;
}
try {
  for (const [name, flags] of [['default', []], ['source', ['--language-source']]]) {
    const out = join(directory, name);
    run(structure, [abc, out, '--value-source', ...flags]);
    const records = JSON.parse(readFileSync(join(out, 'manifest.json'))).records.filter(r => r.source_status !== 2);
    assert.equal(records.length, 1);
    assert.equal(records[0].source_complete, true, JSON.stringify(records[0].source_failure));
    const file = join(out, records[0].source), text = readFileSync(file, 'utf8');
    assert.match(text, /for \([^\n]+ in /);
    run(process.execPath, ['--check', file]);
    const generated = await import(pathToFileURL(file));
    for (const method of ['scan', 'exits', 'keys', 'keysBreak'])
      for (const object of objects) for (const mode of ['continue', 'break', 'return', 'throw'])
        for (const throwing of [false, true]) for (const deletion of [false, true])
          assert.deepEqual(trace(generated[method], object, mode, throwing, deletion),
            trace(oracle[method], object, mode, throwing, deletion), `${name}/${method}/${mode}`);
    assert.equal(generated.keys({}, () => {}), 'initial');
    assert.equal(generated.keys({first: 1, last: 2}, () => {}), 'last');
    assert.equal(generated.keysBreak({}, () => {}, 'break'), 'initial');
    assert.equal(generated.keysBreak({first: 1, last: 2}, () => {}, 'break'), 'first');
    for (const outer of objects.slice(0, 5)) for (const inner of objects.slice(0, 5)) {
      const invoke = fn => { const log = []; fn(outer(log), inner(log), (a, b) => log.push([a, b])); return log; };
      assert.deepEqual(invoke(generated.nested), invoke(oracle.nested));
    }
    // A wrong enumeration of own keys loses inherited keys. This is a source
    // mutation oracle, independent of the original-flow certificate mutations.
    const changed = text.replace(/for \((\$\w+) in (\$\w+)\)/g, 'for ($1 of Object.keys($2))');
    assert.notEqual(changed, text);
    const mutant = await import(`data:text/javascript;base64,${Buffer.from(changed).toString('base64')}`);
    assert.notDeepEqual(trace(mutant.scan, objects[4], 'continue', false), trace(oracle.scan, objects[4], 'continue', false));
    const lost = text.replace(/return (\$phi\d+);/, 'return undefined;');
    assert.notEqual(lost, text);
    const lostKey = await import(`data:text/javascript;base64,${Buffer.from(lost).toString('base64')}`);
    assert.notDeepEqual(trace(lostKey.keys, objects[0], 'continue', false), trace(oracle.keys, objects[0], 'continue', false));
    // Nonempty exhaustion phi copies must not be run on an early-break exit.
    // This representation gap remains a typed refusal, not a weakened oracle.
    const negative=join(directory,`${name}-key-break`);
    run(structure,[rejected,negative,'--value-source',...flags]);
    const refused=JSON.parse(readFileSync(join(negative,'manifest.json'))).records.find(r=>r.source_status!==2);
    assert.equal(refused.source_complete,false);assert.equal(refused.source,undefined);
    assert.equal(refused.source_failure.method,380);assert.equal(refused.source_failure.pc,22);
    assert.deepEqual(refused.source_failure.causes,[['FunctionTreeError',0],['EvaluationError',2],['EvaluationObligation',0],['ForInError',7],['TreeObligation',14]]
      .map(([type,value])=>({domain:`abcd::source::${type}`,value})));

  }
} finally { rmSync(directory, {recursive: true, force: true}); }
console.log('native for-in engine output: ordinary-object differential passed');
