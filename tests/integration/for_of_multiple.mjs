// Engine publication and independent source oracle. Fixture source never enters
// the engine. The raw close certificate/recovery mutations are separate tests.
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';
const [engine, fixtures] = process.argv.slice(2);
const root = mkdtempSync(join(tmpdir(), 'abcd-close-exits-'));
try {
  const out = join(root, 'multi');
  execFileSync(engine, [join(fixtures, 'multi.abc'), out, '--value-source', '--language-source']);
  const records = JSON.parse(readFileSync(join(out, 'manifest.json'))).records.filter(r => r.source_status !== 2);
  assert.equal(records.length, 1);
  assert.equal(records[0].source_status, 0, JSON.stringify(records[0].source_failure));
  const file = join(out, records[0].source);
  execFileSync(process.execPath, ['--check', file]);
  const actual = (await import(pathToFileURL(file))).scan;
  const expected = (await import('data:text/javascript;base64,' +
      Buffer.from(readFileSync(join(fixtures, 'multi.js'))).toString('base64'))).scan;
  assert.equal(actual.length, expected.length);
  const previous=join(root,'previous');
  execFileSync(engine,[join(fixtures,'multi.abc'),previous,'--value-source',
      '--language-numeric','--language-constructors']);
  const old=JSON.parse(readFileSync(join(previous,'manifest.json'))).records.find(r=>r.source_status!==2);
  assert.equal(old.source_status,3);
  assert.ok(old.source_failure.causes.some(c=>c.domain==='abcd::source::ExceptionControlError' && c.value===4));
  const modes = ['normal', 'break', 'return', 'throw', 'continue'];
  const outcomes = ['object', 'missing', 'null', 'getter-throw', 'call-throw', 'primitive'];
  let cases = 0;
  function run(fn, values, mode, outcome, throwAt) {
    const log = [];
    const body = new RangeError('body');
    const saved = globalThis.bodyError;
    globalThis.bodyError = body;
    const input = {[Symbol.iterator]() {
      log.push('acquire');
      let index = 0;
      const iterator = {get next() {
        log.push('next-get');
        return function() {
          log.push(['next', this === iterator]);
          return index < values.length ? {done: false, value: values[index++]} : {done: true};
        };
      }};
      if (outcome !== 'missing') Object.defineProperty(iterator, 'return', {get() {
        log.push('return-get');
        if (outcome === 'getter-throw') throw new SyntaxError('getter');
        if (outcome === 'null') return null;
        return function() {
          log.push(['return-call', this === iterator]);
          if (outcome === 'call-throw') throw new URIError('call');
          return outcome === 'primitive' ? 1 : {};
        };
      }});
      return iterator;
    }};
    let visits = 0;
    try {
      const result = fn(input, x => {
        log.push(['visit', x]);
        if (++visits === throwAt) throw body;
      }, mode);
      log.push(['result', result]);
    } catch (e) {
      log.push(['error', e.constructor.name, e === body]);
    } finally { globalThis.bodyError = saved; }
    return log;
  }
  for (const values of [[], [3], [3, 8]]) for (const mode of modes)
    for (const outcome of outcomes) for (const at of [0, 1, 2]) {
      assert.deepEqual(run(actual, values, mode, outcome, at),
          run(expected, values, mode, outcome, at), `${values}/${mode}/${outcome}/${at}`);
      ++cases;
    }
  // The remaining admission limits are asserted, not counted as semantic passes.
  const gaps = {};
  for (const name of ['liveout', 'labelled', 'async', 'nested', 'catch']) {
    const dest = join(root, name);
    execFileSync(engine, [join(fixtures, name + '.abc'), dest, '--value-source', '--language-source']);
    const record = JSON.parse(readFileSync(join(dest, 'manifest.json'))).records.find(r => r.source_status !== 2);
    assert.equal(record.source_status, 3, name);
    assert.ok(record.source_failure.causes.length > 0, name);
    if (name === 'liveout') assert.ok(record.source_failure.causes.some(c =>
        c.domain === 'abcd::source::TreeObligation' && c.value === 2));
    else assert.ok(record.source_failure.causes.some(c =>
        c.domain === 'abcd::source::ExceptionControlError' && c.value === 4), name);
    gaps[name] = record.source_failure.causes;
  }
  console.log(JSON.stringify({published: 1, differential_cases: cases, refused: gaps}));
} finally { rmSync(root, {recursive: true, force: true}); }
