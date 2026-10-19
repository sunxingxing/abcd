import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';
const [engine, abc, source] = process.argv.slice(2);
const dir = mkdtempSync(join(tmpdir(), 'abcd-for-of-selection-'));
try {
  execFileSync(engine, [abc, dir + '/output', '--value-source', '--language-source']);
  const manifest = JSON.parse(readFileSync(dir + '/output/manifest.json'));
  const record = manifest.records.find(r => r.source_status === 0);
  assert.ok(record, 'engine must publish the full fixture');
  const output = join(dir, 'output', record.source);
  const text = readFileSync(output, 'utf8');
  assert.match(text, /for \([^)]* of /);
  execFileSync(process.execPath, ['--check', output]);
  const actual = await import(pathToFileURL(output));
  const expected = await import('data:text/javascript,' + encodeURIComponent(readFileSync(source, 'utf8')));
  function run(module, values, mode, outside = false) {
    const log = [];
    const stop = label => { log.push(label); if (mode === label) throw new RangeError(label); };
    const source = {get get() { stop('get getter'); return function() {
      stop('get call'); let i = 0;
      return {[Symbol.iterator]() { stop('iterator'); return this; },
        next() { stop('next'); return {done: i === values.length, value: values[i++]}; },
        return() { stop('close'); return {}; }};
    }; }};
    const left = x => { stop('left:' + x); return x === 1; };
    const right = x => { stop('right:' + x); return x === 2 ? {valueOf() { throw new Error('ToBoolean must not call valueOf'); }} : 0; };
    const visit = x => stop('visit:' + x);
    try { const value = outside ? module.outside(values[0], left, right, visit) : module.scan(source, left, right, visit); return {value, log}; }
    catch(e) { return {error: e.constructor.name, log}; }
  }
  for (const values of [[], [1], [2], [3], [1, 2, 3]]) {
    for (const mode of ['', 'get getter', 'get call', 'iterator', 'next', 'left:1', 'left:2', 'right:2', 'right:3', 'visit:1', 'visit:3', 'close']) {
      assert.deepEqual(run(actual, values, mode), run(expected, values, mode), `${values}/${mode}`);
    }
  }
  for (const x of [1, 2, 3]) for (const mode of ['', 'left:' + x, 'right:' + x, 'visit:' + x])
    assert.deepEqual(run(actual, [x], mode, true), run(expected, [x], mode, true));
  assert.deepEqual(run(actual, [1, 2, 3], '').log.filter(x => x.startsWith('right:')), ['right:2', 'right:3']);
} finally { rmSync(dir, {recursive:true, force:true}); }
