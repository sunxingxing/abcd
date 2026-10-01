import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

const source = execFileSync(process.argv[2], ['--emit'], { encoding: 'utf8' });
// Real lexical TDZ and mutable bindings are evaluation inputs only. The engine
// receives decoded ABC operations; no expected source/body enters its pipeline.
const probe = `
export const old = {};
const invoke = fn => { try { return { returned: fn(old) }; } catch (thrown) { return { thrown }; } };
export const firstTdz = literals.map(invoke);
let moduleFirst = { read() {} };
export const secondTdz = literals.map(invoke);
let moduleSecond;
export function replaceSecond(value) { moduleSecond = value; }
export function run(index, first, second, saved) {
  moduleFirst = first; moduleSecond = second; return literals[index](saved);
}
`;
const load = async text => import(`data:text/javascript;base64,${Buffer.from(text + probe).toString('base64')}`);
const values = [undefined, null, false, -0, NaN, 2n, Symbol('value'), {}, new Proxy({}, {})];
let scenarios = 0;
async function check(text, count = true) {
  const generated = await load(text);
  assert.equal(generated.literals.length, 12);
  for (const phase of [generated.firstTdz, generated.secondTdz])
    for (let i = 0; i < phase.length; ++i) {
      if (i % 2) assert.equal(phase[i].returned, generated.old);
      else assert(phase[i].thrown instanceof ReferenceError);
      if (count) ++scenarios;
    }
  for (let i = 0; i < generated.literals.length; ++i) {
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
              return call ? { field0: reference.first.read(), field1: referenceSecond.read() } :
                { field0: reference.first.read, field1: referenceSecond.read };
            } catch (error) { if (caught) return saved; throw error; }
          });
          const actualWorld = world(generated.replaceSecond);
          const actual = observe(() => generated.run(i, actualWorld.first, actualWorld.second, saved));
          assert.deepEqual(actualWorld.log, reference.log);
          assert.deepEqual(Object.keys(actual), Object.keys(expected));
          if ('thrown' in expected) assert(Object.is(actual.thrown, expected.thrown));
          else if (expected.returned === saved) assert.equal(actual.returned, saved);
          else {
            const object = actual.returned;
            assert.equal(Object.getPrototypeOf(object), Object.prototype);
            assert.deepEqual(Reflect.ownKeys(object), ['field0', 'field1']);
            for (const name of ['field0', 'field1']) {
              const descriptor = Object.getOwnPropertyDescriptor(object, name);
              assert(Object.is(descriptor.value, expected.returned[name]));
              assert(descriptor.writable && descriptor.enumerable && descriptor.configurable);
            }
            assert.notEqual(object, generated.run(i, { read: call ? () => value : value },
                { read: call ? () => value : value }, saved));
          }
          if (count) ++scenarios;
        }
  }
}
await check(source);
const mutations = [
  source.replaceAll('moduleFirst.read', '(moduleFirst.read, moduleFirst.read)'),
  source.replaceAll('moduleFirst', 'temporaryBinding').replaceAll('moduleSecond', 'moduleFirst')
    .replaceAll('temporaryBinding', 'moduleSecond'),
  source.replaceAll('moduleFirst.read()', 'moduleFirst.read'),
];
for (const mutation of mutations) {
  assert.notEqual(mutation, source);
  await assert.rejects(() => check(mutation, false));
}
console.log(`module literals: ${scenarios} scenarios and ${mutations.length} mutations passed`);
