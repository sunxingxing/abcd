import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {createHash} from 'node:crypto';
const [engine, fixture, semantic] = process.argv.slice(2);
const temporary = mkdtempSync(join(tmpdir(), 'abcd-staged-indexed-'));
const run = (command, args) => {
  const p = spawnSync(command, args, {encoding: 'utf8'});
  assert.equal(p.status, 0, p.stderr);
  return p.stdout;
};
try {
  const manifest = JSON.parse(readFileSync(join(fixture, 'manifest.json')));
  for (const [file, hash] of Object.entries(manifest.hashes))
    assert.equal(createHash('sha256').update(readFileSync(join(fixture, file))).digest('hex'), hash);
  const output = join(temporary, 'output');
  run(engine, [join(fixture, 'arrays.abc'), output, '--value-source', '--language-source']);
  const record = JSON.parse(readFileSync(join(output, 'manifest.json'))).records.find(r => r.source_status === 0);
  assert.ok(record, 'engine must recover the whole fixture record');
  const metadata = JSON.parse(readFileSync(join(output, record.metadata)));
  const functions = metadata.methods.filter(m => ['strings','values','several'].includes(m.runtime_name));
  assert.equal(functions.length,3);
  for (const method of functions) {
    const instructions = run(semantic,[join(fixture,'arrays.abc'),String(method.entity),'--instructions']).trim().split('\n').map(JSON.parse);
    const definitions = instructions.filter(i=>i.indexed_definition);
    assert.equal(definitions.length,method.runtime_name==='several'?2:1);
    assert.deepEqual(definitions.map(i=>i.indexed_definition.index),method.runtime_name==='several'?[0,1]:[0]);
    for (const definition of definitions) {
      assert.equal(definition.exceptional_completion,1);
      assert.equal(definition.exception_edge.propagates_to_caller,true);
    }
  }
  const emitted = join(output, record.source);
  run(process.execPath, ['--check', emitted]);
  const original = join(temporary, 'original.mjs');
  writeFileSync(original, readFileSync(join(fixture, 'arrays.js')));
  const runner = join(temporary, 'run.mjs');
  writeFileSync(runner, `
const m = await import(process.argv[2]); let trace = ''; let cases = 0;
for (const name of ['strings', 'values', 'several'])
for (const mode of ['normal', 'setter', 'before-throw', 'after-throw', 'flag-throw', 'mode-throw', 'key-throw', 'value-throw', 'left-throw', 'right-throw', 'callee-throw', 'noncallable', 'replace'])
for (const flag of [false, true]) for (const choice of [0, 1, 2]) {
  trace += JSON.stringify([name, mode, flag, choice]) + ':';
  const input = {}; const sink = {}; let invocations = 0, setterHits = 0;
  const event = text => {trace += text + ';';};
  for (const key of ['before', 'after', 'flag', 'mode', 'second', 'key', 'dynamic', 'other'])
    Object.defineProperty(input, key, {get() {
      event(key); if (mode === key + '-throw' || (mode === 'value-throw' && (key === 'dynamic' || key === 'other'))) throw new RangeError();
      if (mode === 'replace') Object.defineProperty(sink, 'f', {value() {event('WRONG replacement');}, configurable: true});
      return key === 'flag' || key === 'second' ? flag : key === 'mode' ? choice : key === 'key' ? 'dynamic' : key;
    }});
  for (const key of ['left', 'right']) input[key] = function() {event(key + ':' + (this === input)); if (mode === key + '-throw') throw new URIError(); return key;};
  Object.defineProperty(sink, 'f', {configurable: true, get() {
    event('callee'); if (mode === 'callee-throw') throw new SyntaxError(); if (mode === 'noncallable') return 13;
    return function(value) {
      ++invocations; event('invoke:' + (this === sink));
      const array = value.value;
      for (let i = 0; i < array.length; ++i) {
        const d = Object.getOwnPropertyDescriptor(array, String(i));
        if (!d || !d.writable || !d.enumerable || !d.configurable) throw new Error('wrong descriptor');
      }
      return value;
    };
  }});
  // No array.push or host logging while the hostile inherited setter is active.
  // A native literal uses CreateDataProperty; an indexed Set would hit it.
  if (mode === 'setter') Object.defineProperty(Array.prototype, '0', {configurable: true, set() {++setterHits; throw new EvalError();}});
  let result, error;
  try {result = m[name](sink, input);} catch (e) {error = e.constructor.name;}
  finally {if (mode === 'setter') delete Array.prototype[0];}
  if (setterHits !== 0) throw new Error('inherited indexed setter observed');
  trace += JSON.stringify({result, error, invocations, setterHits}) + '\\n'; ++cases;
}
// Every invocation allocates a fresh array, even with identical selected values.
for (const name of ['strings', 'values', 'several']) {
 const sink={f(x){return x.value;}}; const input={flag:true,mode:1,second:true,left(){return 1;},right(){return 2;},key:'dynamic',dynamic:3};
 if(m[name](sink,input)===m[name](sink,input)) throw new Error('shared activation');
}
console.log(JSON.stringify({cases,trace}));`);
  const expected = run(process.execPath, [runner, original]);
  const actual = run(process.execPath, [runner, emitted]);
  assert.equal(actual, expected);
  assert.equal(JSON.parse(actual).cases, 234);
  // Behavioral mutation: replace an own literal definition by indexed Set.
  const source = readFileSync(emitted, 'utf8');
  const mutated = source.replace('value: [$arg1.flag ? $arg1[$arg1.key] : $arg1.other]',
    'value: ((v) => { const a = []; a[0] = v; return a; })($arg1.flag ? $arg1[$arg1.key] : $arg1.other)');
  assert.notEqual(mutated, source, 'mutation must modify engine output');
  const mutation = join(temporary, 'mutation.mjs'); writeFileSync(mutation, mutated);
  const rejected = spawnSync(process.execPath, [runner, mutation], {encoding:'utf8'});
  assert.notEqual(rejected.status, 0, 'inherited setter must detect Set substitution');
  assert.match(rejected.stderr, /inherited indexed setter observed/);
  console.log('234 staged indexed-array engine/original scenarios; own descriptors, hostile inherited setter, timing, throws, receiver, callee replacement and Set mutation passed');
} finally {rmSync(temporary, {recursive:true, force:true});}
