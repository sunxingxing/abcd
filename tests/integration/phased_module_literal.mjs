import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

const source = execFileSync(process.argv[2], ['--emit'], { encoding: 'utf8' });
// Real lexical TDZ and mutable bindings are evaluation inputs only. The engine
// receives decoded ABC operations; no expected source/body enters its pipeline.
const probe = `
export const old = {};
const invoke = fn => { try { return { returned: fn(old) }; } catch (thrown) { return { thrown }; } };
export const firstTdz = literals.map(invoke);
export const firstArrayTdz = arrays.map(invoke);
let moduleFirst = { read() {} };
export const secondTdz = literals.map(invoke);
export const secondArrayTdz = arrays.map(invoke);
let moduleSecond;
export function replaceSecond(value) { moduleSecond = value; }
export function run(index, first, second, saved, array) {
  moduleFirst = first; moduleSecond = second; return (array ? arrays : literals)[index](saved);
}
`;
const load = async text => import(`data:text/javascript;base64,${Buffer.from(text + probe).toString('base64')}`);
const values = [undefined, null, false, -0, NaN, 2n, Symbol('value'), {}, new Proxy({}, {})];
let scenarios = 0;
async function check(text, count = true) {
  const generated = await load(text);
  assert.equal(generated.literals.length, 12);
  assert.equal(generated.arrays.length, 12);
  for (const phase of [generated.firstTdz, generated.secondTdz, generated.firstArrayTdz, generated.secondArrayTdz])
    for (let i = 0; i < phase.length; ++i) {
      if (i % 2) assert.equal(phase[i].returned, generated.old);
      else assert(phase[i].thrown instanceof ReferenceError);
      if (count) ++scenarios;
    }
  for (const array of [false, true]) for (let i = 0; i < generated.literals.length; ++i) {
    const call = Math.floor(i / 2) % 2 === 1;
    const caught = i % 2 === 1;
    for (const value of values)
      for (const failure of [null, 'first-get', 'first-call', 'second-get', 'second-call'])
        for (const replace of [false, true]) {
          if (!call && failure?.endsWith('call')) continue;
          const saved = {};
          const world = replaceBinding => {
            const log = [];
            const object = label => {
              const receiver = {};
              Object.defineProperty(receiver, 'read', { get() {
                assert.equal(this, receiver); log.push(`${label}-get`);
                if (label === 'first' && replace && !call) replaceBinding(replacement);
                if (failure === `${label}-get`) throw value;
                if (!call) return value;
                return function () {
                  assert.equal(this, receiver); assert.equal(arguments.length, 0);
                  log.push(`${label}-call`);
                  if (label === 'first' && replace) replaceBinding(replacement);
                  if (failure === `${label}-call`) throw value;
                  return value;
                };
              } });
              return receiver;
            };
            const first = object('first'), second = object('second'), replacement = object('replacement');
            return { first, second, log };
          };
          let referenceSecond;
          const reference = world(value => { referenceSecond = value; });
          referenceSecond = reference.second;
          const observe = fn => { try { return { returned: fn() }; } catch (thrown) { return { thrown }; } };
          const expected = observe(() => {
            try {
              if (array) return call ? [reference.first.read(), referenceSecond.read()] :
                [reference.first.read, referenceSecond.read];
              return call ? { field0: reference.first.read(), field1: referenceSecond.read() } :
                { field0: reference.first.read, field1: referenceSecond.read };
            } catch (error) { if (caught) return saved; throw error; }
          });
          const actualWorld = world(generated.replaceSecond);
          const actual = observe(() => generated.run(i, actualWorld.first, actualWorld.second, saved, array));
          assert.deepEqual(actualWorld.log, reference.log);
          assert.deepEqual(Object.keys(actual), Object.keys(expected));
          if ('thrown' in expected) assert(Object.is(actual.thrown, expected.thrown));
          else if (expected.returned === saved) assert.equal(actual.returned, saved);
          else {
            const object = actual.returned;
            assert.equal(Object.getPrototypeOf(object), array ? Array.prototype : Object.prototype);
            assert.equal(Array.isArray(object), array);
            assert.deepEqual(Reflect.ownKeys(object), array ? ['0', '1', 'length'] : ['field0', 'field1']);
            if (array) {
              assert.equal(object.length, 2);
              assert.deepEqual(Object.getOwnPropertyDescriptor(object, 'length'),
                { value: 2, writable: true, enumerable: false, configurable: false });
            }
            for (const name of array ? ['0', '1'] : ['field0', 'field1']) {
              const descriptor = Object.getOwnPropertyDescriptor(object, name);
              assert(Object.is(descriptor.value, expected.returned[name]));
              assert(descriptor.writable && descriptor.enumerable && descriptor.configurable);
            }
            assert.notEqual(object, generated.run(i, { read: call ? () => value : value },
                { read: call ? () => value : value }, saved, array));
          }
          if (count) ++scenarios;
        }
  }
}
await check(source);
const mutations = [];
const arrayBoundary = source.indexOf('export const arrays');
assert(arrayBoundary > 0);
for (const arraysOnly of [false, true]) {
  const prefix = arraysOnly ? source.slice(0, arrayBoundary) : '';
  const body = arraysOnly ? source.slice(arrayBoundary) : source;
  mutations.push(
    prefix + body.replaceAll('moduleFirst.read', '(moduleFirst.read, moduleFirst.read)'),
    prefix + body.replaceAll('moduleFirst', 'temporaryBinding').replaceAll('moduleSecond', 'moduleFirst')
      .replaceAll('temporaryBinding', 'moduleSecond'),
    prefix + body.replaceAll('moduleFirst.read()', 'moduleFirst.read'));
}
for (const mutation of mutations) {
  assert.notEqual(mutation, source);
  await assert.rejects(() => check(mutation, false));
}
console.log(`module object/array literals: ${scenarios} scenarios and ${mutations.length} mutations passed`);
