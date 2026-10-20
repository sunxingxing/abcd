import assert from 'node:assert/strict';
import {execFileSync, spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {dirname, join} from 'node:path';
import {pathToFileURL} from 'node:url';
const [engine, abc, semantic] = process.argv.slice(2);
const dir = mkdtempSync(join(tmpdir(), 'abcd-iteration-context-'));
try {
  execFileSync(engine, [abc, dir + '/out', '--value-source', '--language-source']);
  const manifest = JSON.parse(readFileSync(dir + '/out/manifest.json'));
  const record = manifest.records.find(r => r.entity === 299);
  assert.equal(record.source_status, 0, JSON.stringify(record));
  const path = join(dir, 'out', record.source);
  execFileSync(process.execPath, ['--check', path]);
  const actual = await import(pathToFileURL(path));
  const oracle = await import('data:text/javascript,' + encodeURIComponent(readFileSync(join(dirname(abc), 'main.js'), 'utf8')));
  function run(module, empty, fail) {
    const log = [];
    const step = text => { log.push(text); if (text === fail) throw new RangeError(text); };
    const object = empty ? {} : Object.assign(Object.create({inherited: 1}), {own: 2});
    const source = {get get() {
      step('get lookup'); return function() {
        assert.equal(this, source); step('get call');
        const values = empty ? [] : ['x', 'y']; let i = 0;
        return {[Symbol.iterator]() {step('iterator'); return this;},
          next() {step('next'); return {done: i === values.length, value: values[i++]};},
          return() {step('close'); return {};}};
      };
    }};
    try {return {value: module.scan(object, source, key => {step('visit:' + key); return key;}), log};}
    catch (e) {return {error: e.constructor.name, log};}
  }
  let cases = 0;
  for (const empty of [false, true])
    for (const fail of ['', 'get lookup', 'get call', 'iterator', 'next', 'visit:own', 'visit:inherited', 'visit:x', 'visit:y', 'close']) {
      assert.deepEqual(run(actual, empty, fail), run(oracle, empty, fail), `${empty}/${fail}`);
      ++cases;
    }
  assert.deepEqual(run(actual, false, '').value, [['own', 'inherited', 'x', 'y'], 'inherited', 'y']);
  assert.deepEqual(run(actual, true, '').value, [[], undefined, undefined]);
  const inspection = spawnSync(semantic, [abc, '395', '--record-source', '--language-source'], {encoding:'utf8'});
  assert.equal(inspection.status, 0);
  const inspected = JSON.parse(inspection.stdout);
  assert.equal(inspected.source_complete, true);
  assert.equal(inspected.source_failure, undefined);
  assert.equal(inspected.source, readFileSync(path, 'utf8'));
  console.log(`shared enumeration context: ${cases} differential cases`);
} finally { rmSync(dir, {recursive:true, force:true}); }
