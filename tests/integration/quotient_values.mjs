import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

// Independent numeric expectations remain outside the engine. Only complete
// functions emitted through the common source tree are executed here.
const source = execFileSync(process.argv[2], ['--source'], {encoding: 'utf8'});
assert.ok(!source.includes('INCOMPLETE'));
assert.ok(source.includes('/'));
const {default: functions} = await import(`data:text/javascript,${encodeURIComponent(source)}`);
const operands = [[64, 2.5], [-7, 2], [-0, 2], [1, -0],
  [1, Infinity], [-Infinity, 2], [Number.MIN_VALUE, 2]];
assert.equal(functions.length, operands.length);
for (let index = 0; index < functions.length; ++index) {
  const [left, right] = operands[index];
  assert.ok(Object.is(functions[index](), left / right), `quotient ${index}`);
}
console.log(`Verified ${functions.length} complete generated quotients, including signed zero, infinity and underflow.`);
