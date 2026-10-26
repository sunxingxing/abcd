// Differential constructor completion: native errors compare by constructor;
// explicit user exceptions additionally retain their message and identity.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
let cases = 0;
const describe = v => v === null ? 'null' : typeof v === 'object' ? 'object' : typeof v === 'function' ? 'function' : v;
const observe = (m, name, args, alternate = false, throwAt = 0, replacement = false) => {
  const log = [], sentinel = new Error('parent exception');
  function Alternate() {}
  function Parent(...values) {
    log.push(['parent', values.map(describe), new.target === Alternate]);
    if (throwAt && log.length === throwAt) throw sentinel;
    if (replacement) return {replacement: true};
  }
  const C = m[name], previous = Object.getPrototypeOf(C);
  Object.setPrototypeOf(C, Parent);
  try {
    const value = Reflect.construct(C, args, alternate ? Alternate : C);
    return {kind: 'return', value: describe(value), keys: Object.keys(value),
      argumentIdentity: value === args[1], alternate: value instanceof Alternate,
      replacement: value.replacement === true, x: value.x, log};
  } catch (e) {
    return {kind: 'throw', error: e.constructor.name,
      user: e === sentinel, message: e === sentinel ? e.message : undefined, log};
  } finally { Object.setPrototypeOf(C, previous); }
};
const check = (name, args, options = [], expected) => {
  const left = observe(a, name, args, ...options), right = observe(b, name, args, ...options);
  assert.deepEqual(right, left, `${name} ${args.map(describe)}`);
  if (expected) assert.equal(left.kind === 'throw' ? left.error : left.kind, expected);
  cases++; return left;
};
for (const d of ['s', '', false, 0, null, undefined, {}, () => 1])
  check('StringBranch', [d], [], typeof d === 'string' ? 'return' : 'TypeError');
for (const d of [true, false, 0, 's']) check('BothBranches', [d], [], 'return');
for (const d of [true, false, 0, 's']) check('EarlyObject', [d], [], 'return');
for (const init of [false, true]) {
  for (const value of [{v: 1}, () => 7, undefined, null, 0, false, 'primitive'])
    check('ReturnValue', [init, value], [], value === undefined ? (init ? 'return' : 'ReferenceError') :
      value !== null && (typeof value === 'object' || typeof value === 'function') ? 'return' : 'TypeError');
  for (const name of ['BareReturn', 'Fallthrough', 'ReadThis'])
    check(name, [init], [], init ? 'return' : 'ReferenceError');
}
const twice = check('DoubleSuper', [4], [], 'ReferenceError');
assert.equal(twice.log.length, 2);
check('DoubleSuper', [4], [false, 2], 'Error');
for (const n of [0, 1, 2, 4]) check('LoopSuper', [n], [], n === 1 ? 'return' : 'ReferenceError');
for (const name of ['StringBranch', 'BothBranches', 'EarlyObject', 'ReturnValue', 'LoopSuper']) {
  const args = name === 'StringBranch' ? ['s'] : name === 'ReturnValue' ? [true, undefined] : [1];
  check(name, args, [true], 'return');
  check(name, args, [false, 1], 'Error');
  check(name, args, [false, 0, true], 'return');
}
console.log(JSON.stringify({cases}));
