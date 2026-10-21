import {recordNodeEvidence} from '../support/isa/oracle_evidence.mjs';
// Engine publication and independent source oracle. Fixture source never enters
// the engine. The raw close certificate/recovery mutations are separate tests.
import assert from 'node:assert/strict';
import {traceAsyncForOf} from '../support/for_of_async_oracle.mjs';
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
  const caughtOut=join(root,'catch');
  execFileSync(engine,[join(fixtures,'catch.abc'),caughtOut,'--value-source','--language-source']);
  const caughtRecord=JSON.parse(readFileSync(join(caughtOut,'manifest.json'))).records.find(r=>r.source_status!==2);
  assert.equal(caughtRecord.source_status,0,JSON.stringify(caughtRecord.source_failure));
  const caughtFile=join(caughtOut,caughtRecord.source);
  execFileSync(process.execPath,['--check',caughtFile]);
  const caughtActual=(await import(pathToFileURL(caughtFile))).scan;
  const caughtExpected=(await import('data:text/javascript;base64,'+Buffer.from(readFileSync(join(fixtures,'catch.js'))).toString('base64'))).scan;
  for(const values of [[],[3],[3,8]])for(const mode of ['normal','break','return'])for(const outcome of outcomes)for(const at of [0,1,2]) {
    assert.deepEqual(run(caughtActual,values,mode,outcome,at),run(caughtExpected,values,mode,outcome,at),`catch/${values}/${mode}/${outcome}/${at}`);++cases;
  }
  const asyncOut=join(root,'async');
  execFileSync(engine,[join(fixtures,'async.abc'),asyncOut,'--value-source','--language-source']);
  const asyncRecord=JSON.parse(readFileSync(join(asyncOut,'manifest.json'))).records.find(r=>r.source_status!==2);
  assert.equal(asyncRecord.source_status,0,JSON.stringify(asyncRecord.source_failure));
  const asyncFile=join(asyncOut,asyncRecord.source);execFileSync(process.execPath,['--check',asyncFile]);
  const asyncActual=(await import(pathToFileURL(asyncFile))).scan;
  const asyncExpected=(await import('data:text/javascript;base64,'+Buffer.from(readFileSync(join(fixtures,'async.js'))).toString('base64'))).scan;
  for(const values of [[],[3],[3,8]])for(const mode of ['normal','break','return'])
    for(const outcome of outcomes)for(const at of [0,1,2])for(const rejected of [false,true]) {
      assert.deepEqual(await traceAsyncForOf(asyncActual,values,mode,outcome,at,rejected),await traceAsyncForOf(asyncExpected,values,mode,outcome,at,rejected),`async/${values}/${mode}/${outcome}/${at}/${rejected}`);++cases;
    }
  // The remaining admission limits are asserted, not counted as semantic passes.
  const gaps = {};
  for (const name of ['liveout', 'labelled', 'nested']) {
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
  recordNodeEvidence({abc:join(fixtures,'multi.abc'),output:out,original:join(fixtures,'multi.js'),producer:import.meta.url,cases:270,engine});
  recordNodeEvidence({abc:join(fixtures,'catch.abc'),output:caughtOut,original:join(fixtures,'catch.js'),producer:import.meta.url,cases:162,engine});
  recordNodeEvidence({abc:join(fixtures,'async.abc'),output:asyncOut,original:join(fixtures,'async.js'),producer:import.meta.url,cases:324,engine});
  console.log(JSON.stringify({published: 3, differential_cases: cases, refused: gaps}));
} finally { rmSync(root, {recursive: true, force: true}); }
