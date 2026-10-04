import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
const [tool, baseABC, derivedABC] = process.argv.slice(2);
const referenceBody = {
  direct: 'return this;',
  event: 'event(); return this;',
  'missing-super': 'return this;',
  value: 'return value;',
  call: 'return event();',
  sequence: 'return (event(), this);',
  early: 'return this; event();',
  nested: '{ return this; } event();',
  'catch-before-super': 'try { return this; } catch (error) { event(); return value; }',
  finally: 'try { return this; } finally { event(); }',
  alias: 'const receiver = this; return receiver;',
  member: 'return this.value;',
};
const sentinel = {}, eventResult = {}, superResult = {};
const values = [undefined, null, false, 0, -0, NaN, 'text', 1n, Symbol('value'), {}];
let scenarios = 0, fixtures = 0;
function observe(source, derived, value, eventMode, superMode, destination, getterThrows) {
  const events = [];
  class Base {
    constructor() {
      events.push('super');
      if (superMode === 'throw') throw sentinel;
      if (superMode === 'replace') return superResult;
    }
  }
  const C = Function('Base', `return class Actual ${derived ? 'extends Base' : ''} { ${source} };`)(Base);
  Object.defineProperty(derived ? Base.prototype : C.prototype, 'value', {
    get() { events.push('get'); if (getterThrows) throw sentinel; return value; },
  });
  const alternatePrototype = {};
  const alternate = new Proxy(function Alternate() {}, {
    get(target, key, receiver) {
      if (key === 'prototype') {
        events.push('prototype');
        if (destination === 'throw-prototype') throw sentinel;
        return alternatePrototype;
      }
      return Reflect.get(target, key, receiver);
    },
  });
  let outcome;
  try {
    const result = Reflect.construct(C, [function() {
      events.push('event');
      assert.equal(this, undefined);
      if (eventMode === 'throw') throw sentinel;
      return eventMode === 'object' ? eventResult : value;
    }, value], destination === 'ordinary' ? C : alternate);
    if (result === value) outcome = 'payload';
    else if (result === eventResult) outcome = 'event-result';
    else if (result === superResult) outcome = 'super-result';
    else {
      assert.equal(Object.getPrototypeOf(result), destination === 'ordinary' ? C.prototype : alternatePrototype);
      assert.deepEqual(Reflect.ownKeys(result), []);
      outcome = 'receiver';
    }
  } catch (error) {
    if (error === sentinel) outcome = 'sentinel';
    else if (error instanceof ReferenceError) outcome = 'ReferenceError';
    else if (error instanceof TypeError) outcome = 'TypeError';
    else throw error;
  }
  return {events, outcome};
}
for (const [abc, entity, derived] of [[baseABC, '7312', false], [derivedABC, '6100', true]]) {
  const run = spawnSync(tool, [abc, entity, '--tail-syntax'], {encoding: 'utf8', timeout: 60_000});
  assert.equal(run.status, 0, run.stderr);
  const cases = run.stdout.trim().split('\n').map(line => JSON.parse(line));
  assert.equal(cases.length, Object.keys(referenceBody).length);
  assert.equal(new Set(cases.map(item => item.case)).size, cases.length);
  for (const item of cases) {
    ++fixtures;
    assert.equal(item.derived, derived);
    assert.ok(Object.hasOwn(referenceBody, item.case));
    const body = item.positions.statements[0];
    const returned = item.positions.statements[item.return];
    const operand = item.positions.expressions[item.operand];
    if (item.omitted) {
      assert.doesNotMatch(item.source, /return\s+this\s*;/, item.case);
      assert.equal(returned.begin, returned.end);
      assert.equal(operand.begin, returned.begin);
      assert.equal(operand.end, returned.end);
      assert.ok(body.begin <= returned.begin && returned.end < body.end);
      assert.match(item.source.slice(returned.end), /^\s*}\s*$/);
      assert.equal(returned.emission, 'tail_completion');
      assert.equal(operand.emission, 'tail_completion');
      for (const origin of item.positions.instruction_sources) {
        assert.equal(origin.emission, 'tail_completion');
        assert.deepEqual(origin.range, {begin: returned.begin, end: returned.end});
      }
    } else {
      assert.match(item.source, /return\b/, item.case);
      assert.ok(returned.begin < returned.end && operand.begin < operand.end);
      assert.equal(returned.emission, undefined);
      assert.equal(operand.emission, undefined);
    }
    const prefix = derived && !['missing-super', 'catch-before-super'].includes(item.case) ? 'super(value); ' : '';
    const reference = `constructor(event, value) { ${prefix}${referenceBody[item.case]} }`;
    for (const value of values)
      for (const eventMode of ['value', 'object', 'throw'])
        for (const superMode of derived ? ['normal', 'replace', 'throw'] : ['normal'])
          for (const destination of ['ordinary', 'alternate', 'throw-prototype'])
            for (const getterThrows of [false, true]) {
              assert.deepEqual(observe(item.source, derived, value, eventMode, superMode, destination, getterThrows),
                observe(reference, derived, value, eventMode, superMode, destination, getterThrows),
                `${derived ? 'derived' : 'base'} ${item.case} ${eventMode} ${superMode} ${destination}`);
              ++scenarios;
            }
    const C = Function('Base', `return class ${derived ? 'extends Base' : ''} { ${item.source} };`)(class {});
    assert.equal(C.length, 2);
    assert.throws(() => Reflect.apply(C, {}, []), TypeError);
  }
}
assert.equal(fixtures, 24);
assert.equal(scenarios, 8640);
console.log(JSON.stringify({fixtures, scenarios}));
