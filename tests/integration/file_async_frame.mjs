import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

for (const mode of ['self', 'self-private', 'this', 'receiver']) {
  const source = execFileSync(process.argv[2], [`--await-${mode}`], {encoding: 'utf8', timeout: 60_000});
  assert.ok(!source.includes('INCOMPLETE'));
  const {functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
  assert.equal(functions.length, 9);
  const self = mode.startsWith('self');
  for (const [index, capture] of functions.entries()) {
    const saved = [], initial = {};
    let iteration = 0;
    capture(() => iteration++ === 0, () => {
      if (index % 3 === 1) throw initial;
      return initial;
    }, get => saved.push(get));
    assert.equal(saved.length, 1);
    const get = saved[0];
    assert.equal(get.name, mode === 'self-private' ? 'get value' : 'get');
    assert.equal(get.length, 1);
    assert.equal(Object.hasOwn(get, 'prototype'), false);
    assert.throws(() => Reflect.construct(get, []), TypeError);

    for (const receiver of [undefined, null, false, 0, NaN, 'text', Symbol('this'), {}]) {
      for (const rejects of [false, true]) {
        const error = {};
        async function observe(recovered) {
          const log = [];
          const oracle = async function (input) { await input; return self ? oracle : this; };
          const fn = recovered ? get : oracle;
          const input = {get then() {
            log.push('get-then');
            return function (resolve, reject) {
              assert.equal(this, input); log.push('call-then');
              if (rejects) reject(error); else resolve();
            };
          }};
          const pending = Reflect.apply(fn, receiver, [input]);
          log.push('returned');
          let value, rejected = false;
          const observed = pending.then(result => {
            value = result; log.push('fulfilled');
          }, result => { value = result; rejected = true; log.push('rejected'); });
          queueMicrotask(() => log.push('external-microtask'));
          await observed;
          assert.equal(rejected, rejects);
          assert.ok(Object.is(value, rejects ? error : self ? fn : receiver));
          return log;
        }
        assert.deepEqual(await observe(true), await observe(false));
      }
    }

    // Two concurrent invocations must retain distinct this values, even when
    // resumed in reverse order. The callable identity stays the original one.
    const first = {}, second = {};
    let resumeFirst, resumeSecond, firstDone = false;
    const a = get.call(first, new Promise(resolve => { resumeFirst = resolve; }));
    const b = get.call(second, new Promise(resolve => { resumeSecond = resolve; }));
    a.then(() => { firstDone = true; });
    resumeSecond();
    assert.equal(await b, self ? get : second);
    assert.equal(firstDone, false);
    resumeFirst();
    assert.equal(await a, self ? get : first);
  }
}
