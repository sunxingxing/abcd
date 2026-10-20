import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, rmSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {tmpdir} from 'node:os';
import {join, dirname} from 'node:path';
import {pathToFileURL} from 'node:url';
const [structure, fixture] = process.argv.slice(2);
const temp = mkdtempSync(join(tmpdir(), 'abcd-for-of-cells-'));
const run = (exe, args) => {
  const r = spawnSync(exe, args, {encoding: 'utf8', timeout: 120000});
  assert.equal(r.status, 0, r.stderr + r.stdout); return r.stdout;
};
try {
  const manifest = JSON.parse(readFileSync(join(fixture, 'manifest.json')));
  for (const [file, sha] of Object.entries(manifest.hashes))
    assert.equal(createHash('sha256').update(readFileSync(join(fixture, file))).digest('hex'), sha);
  const exportRecord = name => {
    const out = join(temp, name);
    run(structure, [join(fixture, name + '.abc'), out, '--value-source', '--language-source']);
    const rows = JSON.parse(readFileSync(join(out, 'manifest.json'))).records;
    const record = rows.find(r => JSON.parse(readFileSync(join(out, r.metadata))).methods.length);
    assert.ok(record); return {record, out};
  };
  const {record, out} = exportRecord('callback');
  assert.equal(record.source_complete, true, JSON.stringify(record.source_failure));
  const path = join(out, record.source), text = readFileSync(path, 'utf8');
  assert.match(text, /for \([^)]* of /); run(process.execPath, ['--check', path]);
  const emitted = (await import(pathToFileURL(path))).scan;
  const original = (await import('data:text/javascript;base64,' +
    readFileSync(join(fixture, 'callback.js')).toString('base64'))).scan;
  const trace = (fn, make, throwAt = 0) => {
    const log = []; let n = 0, helper;
    try {
      const value = fn(make(log), (key, update) => {
        log.push(['body', key, helper === undefined || helper === update]); helper = update;
        log.push(['acc', update(key)]);
        if (++n === throwAt) throw new RangeError('body');
      });
      log.push(['result', value]);
    } catch (e) { log.push(['throw', e.constructor.name]); }
    return log;
  };
  let cases = 0;
  const check = (make, at = 0) => {
    assert.deepEqual(trace(emitted, make, at), trace(original, make, at)); ++cases;
  };
  for (const values of [[], [1], [1, 2, 3], [0, -1, 4]])
    for (const at of [0, 1, 2]) check(() => values, at);
  for (const close of ['object', 'missing', 'null', 'getter-throw', 'call-throw', 'primitive'])
    for (const at of [0, 1, 2]) check(log => ({[Symbol.iterator]() {
      log.push('acquire'); let i = 0;
      const iterator = {get next() { log.push('next-get'); return function () {
        log.push(['next', this === iterator]); return i++ < 3 ? {done: false, value: i} : {done: true};
      }; }};
      if (close !== 'missing') Object.defineProperty(iterator, 'return', {get() {
        log.push('return-get'); if (close === 'getter-throw') throw new SyntaxError();
        if (close === 'null') return null;
        return function () { log.push(['return-call', this === iterator]);
          if (close === 'call-throw') throw new TypeError(); return close === 'primitive' ? 1 : {}; };
      }});
      return iterator;
    }}), at);
  for (const where of ['next-get', 'next-call', 'done-get', 'value-get']) check(log => ({[Symbol.iterator]() {
    return {get next() {log.push('next-get'); if (where === 'next-get') throw new SyntaxError();
      return function () {log.push('next-call'); if (where === 'next-call') throw new TypeError();
        return {get done() {log.push('done-get'); if (where === 'done-get') throw new URIError(); return false;},
          get value() {log.push('value-get'); if (where === 'value-get') throw new EvalError(); return 1;}};
      };
    }, return() {log.push('close'); return {};}};
  }}), 1);
  let first, second;
  assert.equal(emitted([1, 2], (x, update) => {first = update; update(x);}), 3);
  assert.equal(emitted([5], (x, update) => {second = update; update(x);}), 5);
  assert.notEqual(first, second); assert.equal(first(10), 13); assert.equal(second(0), 5);
  // A real output mutation must change accumulator behavior, not only spelling.
  const mutant = text.replace(/(\$\w+) \+ (\$\w+)/, '$1 - $2');
  assert.notEqual(mutant, text); const changed = join(temp, 'mutant.mjs'); writeFileSync(changed, mutant);
  const bad = (await import(pathToFileURL(changed))).scan;
  assert.notDeepEqual(trace(bad, () => [1, 2]), trace(original, () => [1, 2]));
  // The certified guarded-read/action-region correspondence now permits
  // these function-local captures. Test engine output, not source-only oracles.
  let localCases=0;
  for(const name of ['cells','skip']) {
    const {record,out}=exportRecord(name);assert.equal(record.source_complete,true,JSON.stringify(record.source_failure));
    const output=join(out,record.source);run(process.execPath,['--check',output]);
    const actualModule=await import(pathToFileURL(output));
    const expectedModule=await import('data:text/javascript;base64,'+readFileSync(join(fixture,name+'.js')).toString('base64'));
    const fn=name==='cells'?'scan':'skip';
    const observe=(f,values,close,throwAt)=>{
      const log=[];let calls=0;
      const input={[Symbol.iterator](){let i=0;log.push('acquire');const it={next(){log.push('next');return i<values.length?{done:false,value:values[i++]}:{done:true};}};
        if(close!=='missing')Object.defineProperty(it,'return',{get(){log.push('return-get');if(close==='getter-throw')throw new SyntaxError('close');if(close==='null')return null;return function(){log.push(['return',this===it]);if(close==='call-throw')throw new URIError('close');return close==='primitive'?1:{};};}});return it;}};
      try{log.push(['result',f(input,(key,sum)=>{log.push(['body',key,sum]);if(++calls===throwAt)throw new RangeError('body');})]);}catch(e){log.push(['throw',e.constructor.name]);}
      return log;
    };
    for(const values of [[],[1],[1,2,3]])for(const close of ['object','missing','null','getter-throw','call-throw','primitive'])for(const at of [0,1,2]) {
      assert.deepEqual(observe(actualModule[fn],values,close,at),observe(expectedModule[fn],values,close,at));++localCases;
    }
    assert.equal(actualModule[fn]([1,2],()=>{}),3);assert.equal(actualModule[fn]([7],()=>{}),7);
    const content=readFileSync(output,'utf8'),changed=content.replace(/(\$\w+) \+ (\$\w+)/,'$1 - $2');
    assert.notEqual(changed,content,name+' mutation must change text');const badFile=join(out,'bad-local.mjs');writeFileSync(badFile,changed);run(process.execPath,['--check',badFile]);
    const mutant=(await import(pathToFileURL(badFile)))[fn];assert.notDeepEqual(observe(mutant,[1,2],'object',0),observe(expectedModule[fn],[1,2],'object',0));
  }
  // These stronger forms reach independent downstream gates. Do not count
  // their refusal or source-only oracle as an engine-output differential.
  const expected = {
    stop: [['FunctionTreeError', 0], ['EvaluationError', 3], ['EvaluationObligation', 0], ['TreeObligation', 16]],
    finish: [['FunctionTreeError', 0], ['EvaluationError', 3], ['EvaluationObligation', 0], ['TreeObligation', 16]],
    throwing: [['FunctionTreeError', 0], ['EvaluationError', 3], ['EvaluationObligation', 0], ['TreeObligation', 16]],
  };
  for (const [name, causes] of Object.entries(expected)) {
    const {record} = exportRecord(name); assert.equal(record.source_complete, false);
    assert.deepEqual(record.source_failure.causes.map(c => [c.domain, c.value]),
      causes.map(([domain, value]) => ['abcd::source::' + domain, value]));
    assert.ok(record.source_failure.pc !== undefined);
    if (name === 'stop' || name === 'finish') {
      // Multiple-close ownership/control now verifies. An always-abrupt loop
      // has no bytecode backedge/LoopId, so native emission remains gated.
      assert.equal(record.source_failure.pc, 45);
      assert.match(run(join(dirname(structure),'close_completion_probe'),
          [join(fixture,name+'.abc'),String(record.source_failure.method),'--control']), /protocol-control units=/);
    }
  }
  console.log(`${cases+localCases} engine/original for-of cell scenarios; close failures, iterator getters, helper identity/isolation and mutation; stronger forms keep typed refusals`);
} finally {rmSync(temp, {recursive: true, force: true});}
