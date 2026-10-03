import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

const conditional = process.argv[3] === '--conditional';
const source = execFileSync(process.argv[2], [conditional ? '--conditional-source' : '--staged-source'], { encoding: 'utf8' });
if (conditional) assert.ok(source.includes(' ? ') && !source.includes('if ('));
const recovered = Function(`return ${source}`)();
// Independent bytecode behavior: JEQZ tests false/number zero; the buffered
// own data property is defined twice after the selected value is evaluated.
function reference(input) {
  try {
    const result = { ['__proto__']: null };
    const value = input === false || input === 0 ? null : conditional ? 7 : input.value;
    for (let i = 0; i < 2; ++i) Object.defineProperty(result, '__proto__', {
      value, writable: true, enumerable: true, configurable: true,
    });
    return result;
  } catch (error) { return error; }
}
let cases = 0;
const values = [undefined, null, false, true, 0, -0, 1, NaN, Infinity,
  '', 'text', 1n, Symbol('field'), {}, [], () => {}];
for (const payload of values) for (const throws of [false, true]) {
  const run = fn => {
    const trace = [];
    const input = new Proxy({}, {
      get(target, key, receiver) {
        trace.push([key, receiver === input]);
        if (throws) throw payload;
        return payload;
      },
    });
    const result = fn(input);
    assert.deepEqual(trace, conditional ? [] : [['value', true]]);
    if (throws && !conditional) assert.ok(Object.is(result, payload));
    else {
      assert.equal(Object.getPrototypeOf(result), Object.prototype);
      assert.deepEqual(Reflect.ownKeys(result), ['__proto__']);
      const property = Object.getOwnPropertyDescriptor(result, '__proto__');
      assert.ok(Object.is(property.value, conditional ? 7 : payload));
      assert.deepEqual([property.writable, property.enumerable, property.configurable], [true, true, true]);
    }
    return result;
  };
  run(reference); run(recovered); ++cases;
}
for (const input of [false, 0, -0, true, 1, '', 'text', 1n, Symbol('input')]) {
  assert.deepEqual(recovered(input), reference(input)); ++cases;
}
for (const input of [undefined, null]) {
  if (conditional) assert.deepEqual(recovered(input), reference(input));
  else {
    assert.ok(recovered(input) instanceof TypeError);
    assert.ok(reference(input) instanceof TypeError);
  }
  ++cases;
}
assert.notEqual(recovered(false), recovered(false));
console.log(`${cases} phased staged literal cases passed`);
