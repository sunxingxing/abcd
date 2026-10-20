import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';
const [engine, fixtures] = process.argv.slice(2);
const dir = mkdtempSync(join(tmpdir(), 'abcd-enumeration-composition-'));
async function load(name) {
  const out = join(dir, name);
  execFileSync(engine, [join(fixtures, name + '.abc'), out, '--value-source', '--language-source']);
  const manifest = JSON.parse(readFileSync(join(out, 'manifest.json')));
  const record = manifest.records.find(r => r.source_status !== 2);
  assert.equal(record?.source_status, 0, JSON.stringify(record));
  const path = join(out, record.source);
  execFileSync(process.execPath, ['--check', path]);
  const actual = await import(pathToFileURL(path));
  const expected = await import('data:text/javascript,' + encodeURIComponent(readFileSync(join(fixtures, name + '.js'), 'utf8')));
  for (const key of Object.keys(expected)) if (typeof expected[key] === 'function') {
    assert.equal(actual[key].length, expected[key].length, `${name}/${key}.length`);
    assert.equal(actual[key].name, expected[key].name, `${name}/${key}.name`);
  }
  return [actual, expected];
}
try {
  const [actual, expected] = await load('main');
  function run(module, mode, empty) {
    const log = [], symbol = Symbol.for('enumeration-composition');
    const step = text => {log.push(text); if (text === mode) throw new RangeError(text);};
    const base = empty ? {} : Object.create({inherited: 1});
    if (!empty) Object.defineProperties(base, {own: {value: 2, enumerable: true}, hidden: {value: 3}, [symbol]: {value: 4, enumerable: true}});
    const object = new Proxy(base, {
      ownKeys(target) { step('ownKeys'); return Reflect.ownKeys(target); },
      getOwnPropertyDescriptor(target, key) { step('descriptor:' + String(key)); return Reflect.getOwnPropertyDescriptor(target, key); },
      getPrototypeOf(target) { step('prototype'); return Reflect.getPrototypeOf(target); }
    });
    const old = Object.getOwnPropertySymbols;
    Object.getOwnPropertySymbols = function(receiver) {
      assert.equal(receiver, object); step('symbols'); let i = 0;
      const values = old(receiver);
      return {[Symbol.iterator]() { step('iterator'); return this; }, next() {
        step('next'); return {get done() {step('done'); return i === values.length;}, get value() {step('value'); return values[i++];}};
      }, return() {step('close'); return {};}};
    };
    try { return {value: module.keys(object, key => step('visit:' + String(key))).map(String), log}; }
    catch(e) { return {error: e.constructor.name, log}; }
    finally { Object.getOwnPropertySymbols = old; }
  }
  for (const empty of [false, true]) for (const mode of ['', 'ownKeys', 'prototype', 'descriptor:own', 'descriptor:Symbol(enumeration-composition)', 'symbols', 'iterator', 'next', 'done', 'value', 'visit:own', 'visit:inherited', 'visit:Symbol(enumeration-composition)', 'close'])
    assert.deepEqual(run(actual, mode, empty), run(expected, mode, empty), `mixed/${mode}/${empty}`);
  const normal = run(actual, '', false);
  assert.deepEqual(normal.value, ['own', 'inherited', 'Symbol(enumeration-composition)']);
  assert.ok(run(actual, 'visit:Symbol(enumeration-composition)', false).log.includes('close'));
  const [local, localOracle] = await load('local_transfer');
  function localRun(module, mode, stop, lists) {
    const log = [];
    const step = text => {log.push(text); if (text === mode) throw new RangeError(text);};
    const groups = lists.map((values, group) => ({[Symbol.iterator]() {
      step('iterator:' + group); let i = 0;
      return {next() {step('next:' + group); return {done: i === values.length, value: values[i++]};},
        get return() {step('return getter:' + group); return function() {step('return call:' + group); if (mode === 'close fails') throw new SyntaxError('close'); return {};};}};
    }}));
    try {return {value: module.scan(groups, (value, j) => {
      step('visit:' + value + ':' + j);
      if (mode === 'close fails') throw new TypeError('body');
      return stop && j === 0;
    }), log};} catch(e) {return {error: e.constructor.name, log};}
  }
  let localCases = 0;
  for (const lists of [[], [[]], [[1]], [[1, 2], [3]], [[], [4, 5]]])
    for (const stop of [false, true]) for (const mode of ['', 'iterator:0', 'next:0', 'visit:1:0', 'visit:1:1', 'visit:3:0', 'return getter:0', 'return call:0', 'close fails']) {
      assert.deepEqual(localRun(local, mode, stop, lists), localRun(localOracle, mode, stop, lists), `local/${mode}/${stop}`);
      ++localCases;
    }
  const localNormal = localRun(local, '', true, [[1, 2], [3]]);
  assert.deepEqual(localNormal.value, [1, 2, 3]);
  assert.ok(!localNormal.log.some(x => x.startsWith('return ')), 'inner break must not close iterator');
  assert.equal(localRun(local, 'close fails', true, [[1]]).error, 'TypeError', 'body throw wins over close throw');
  const [decision, oracle] = await load('decision');
  function compare(module, mode, scenario) {
    const log = [];
    const step = text => {log.push(text); if (text === mode) throw new TypeError(text);};
    const tagged = value => value.tag;
    const make = name => {
      function Constructor() {}
      Object.defineProperties(Constructor, {tag: {value: name}, [Symbol.hasInstance]: {get() {
        step('hasInstance getter:' + name); return x => {step('hasInstance call:' + name + ':' + tagged(x)); return scenario !== 'fail:' + name + ':' + tagged(x);};
      }}});
      return Constructor;
    };
    const left = make('left'), right = scenario === 'same' ? left : make('right');
    const a = {tag: 'a', get constructor() {step('constructor:a'); return left;}};
    const b = {tag: 'b', get constructor() {step('constructor:b'); return right;}};
    const predicates = {get isFunction() {step('predicate getter'); return function(x) {
      assert.equal(this, predicates); step('predicate:' + tagged(x));
      return scenario === 'false:' + tagged(x) ? 0 : {valueOf() {throw new Error('ToBoolean must not coerce');}};
    };}};
    try { return {value: module.compare(a, b, predicates), log}; }
    catch(e) {return {error: e.constructor.name, log};}
  }
  const scenarios = ['', 'same', 'false:a', 'false:b', 'false:left', 'false:right', 'fail:left:a', 'fail:right:b', 'fail:left:left', 'fail:right:right'];
  const modes = ['', 'predicate getter', 'predicate:a', 'predicate:b', 'constructor:a', 'constructor:b', 'predicate:left', 'predicate:right', 'hasInstance getter:left', 'hasInstance getter:right', 'hasInstance call:left:a', 'hasInstance call:right:b', 'hasInstance call:left:left', 'hasInstance call:right:right'];
  for (const scenario of scenarios) for (const mode of modes)
    assert.deepEqual(compare(decision, mode, scenario), compare(oracle, mode, scenario), `decision/${scenario}/${mode}`);
  const [sets, setsOracle] = await load('set_equal');
  assert.equal(sets.sameMembers.length, 3);
  function setRun(module, a, b, mode) {
    const log = [];
    const inputs = values => ({forEach(callback) {
      log.push('forEach:' + values.join(','));
      for (const value of values) callback(value);
    }});
    const iteratorPrototype = Object.getPrototypeOf([][Symbol.iterator]());
    const previous = Object.getOwnPropertyDescriptor(iteratorPrototype, 'return');
    Object.defineProperty(iteratorPrototype, 'return', {configurable: true, get() {
      log.push('return getter');
      return function() {log.push('return call'); if (mode === 'close throw') throw new SyntaxError('close'); return {};};
    }});
    try {return {value: module.sameMembers(inputs(a), inputs(b), (x, y) => {
      log.push('compare:' + x + ':' + y);
      if (mode === 'body throw' || mode === 'close throw') throw new TypeError('compare');
      return x === y;
    }), log};} catch(e) {return {error: e.constructor.name, log};}
    finally {if (previous) Object.defineProperty(iteratorPrototype, 'return', previous); else delete iteratorPrototype.return;}
  }
  for (const a of [[], [1], [1, 2], [1, 1]]) for (const b of [[], [1], [2, 1], [3]])
    for (const mode of ['', 'body throw', 'close throw'])
      assert.deepEqual(setRun(sets, a, b, mode), setRun(setsOracle, a, b, mode), `sets/${a}/${b}/${mode}`);
  assert.equal(setRun(sets, [1], [1], 'close throw').error, 'TypeError');
  console.log(`mixed enumeration: 30 cases; local transfers: ${localCases} cases; retained decision: 150 cases; captured arrays: 48 cases`);
} finally { rmSync(dir, {recursive: true, force: true}); }
