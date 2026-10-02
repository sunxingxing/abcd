import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
const load = async tool => {
  const source = execFileSync(tool, ['--source'], {encoding: 'utf8'});
  return (await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`)).default;
};
let scenarios = 0;
for (const {seed, step, subtract, fn} of await load(process.argv[2])) {
  let expected = seed;
  for (let i = 0; i < 2; ++i) expected = subtract ? expected - step : expected + step;
  assert.ok(Object.is(fn(), expected)); ++scenarios;
}
const number = bits => {
  const view = new DataView(new ArrayBuffer(8));
  view.setBigUint64(0, BigInt(`0x${bits}`)); return view.getFloat64(0);
};
for (const {left, right, subtract, fn} of await load(process.argv[3])) {
  const a = number(left), b = number(right), expected = subtract ? a - b : a + b;
  assert.ok(!Number.isNaN(expected));
  assert.ok(Object.is(fn(), expected)); ++scenarios;
}
console.log(`Proved additive expressions: ${scenarios} native integer/float/signed-zero/overflow scenarios passed`);
