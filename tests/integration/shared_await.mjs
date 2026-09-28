import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const source = execFileSync(process.argv[2], ['--await-completion'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!source.includes('INCOMPLETE'));
const {functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(functions.length, 3);
const oracle = async function (input) { return await input; };
const payloads = [undefined, null, false, 0, NaN, Symbol('await'), {}];

async function observe(fn, makeInput) {
  const log = [];
  const input = makeInput(log);
  let promise;
  assert.doesNotThrow(() => { promise = fn(input); });
  assert.ok(promise instanceof Promise);
  log.push('returned');
  let result, rejected = false;
  const done = promise.then(value => { result = value; log.push('fulfilled'); },
    error => { result = error; rejected = true; log.push('rejected'); });
  queueMicrotask(() => log.push('external-microtask'));
  await done;
  return {result, rejected, log};
}

for (const fn of functions) for (const payload of payloads) {
  const cases = [
    () => payload,
    () => Promise.resolve(payload),
    () => Promise.reject(payload),
    log => ({get then() { log.push('get-then'); throw payload; }}),
    ...[false, true].map(reject => log => {
      const input = {get then() {
        log.push('get-then');
        return function (resolve, fail) {
          assert.equal(this, input);
          log.push('call-then');
          (reject ? fail : resolve)(payload);
          // Settlement is once-only even if a thenable misbehaves afterward.
          resolve('ignored'); fail('ignored'); throw new Error('ignored');
        };
      }};
      return input;
    }),
  ];
  for (const makeInput of cases) {
    const expected = await observe(oracle, makeInput);
    const actual = await observe(fn, makeInput);
    assert.equal(actual.rejected, expected.rejected);
    assert.equal(actual.result, expected.result);
    assert.deepEqual(actual.log, expected.log);
  }
}
