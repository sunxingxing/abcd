import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const fixtures = execFileSync(process.argv[2], ['--async-method-json'],
  {encoding: 'utf8', timeout: 60_000}).trim().split('\n').map(line => JSON.parse(line));
assert.equal(fixtures.length, 6);
for (const fixture of fixtures) {
  assert.ok(!fixture.source.includes('INCOMPLETE'));
  // Only the class enclosure is supplied by the test. The entire method body,
  // signature and async/static syntax come from the shared engine printer.
  const module = `export class Recovered { ${fixture.source} }`;
  const {Recovered} = await import(`data:text/javascript;base64,${Buffer.from(module).toString('base64')}`);
  const owner = fixture.static ? Recovered : new Recovered();
  const holder = fixture.static ? Recovered : Recovered.prototype;
  const method = owner.run;
  assert.equal(method.name, 'run');
  assert.equal(method.length, 1);
  assert.equal(Object.hasOwn(method, 'prototype'), false);
  assert.throws(() => Reflect.construct(method, []), TypeError);
  const descriptor = Object.getOwnPropertyDescriptor(holder, 'run');
  assert.equal(descriptor.enumerable, false);
  assert.equal(descriptor.configurable, true);
  assert.equal(descriptor.writable, true);
  assert.equal(await owner.run(), owner);

  for (const receiver of [undefined, null, false, 0, NaN, 'text', Symbol('receiver'), {}]) {
    for (const completion of ['resolve', 'reject', 'getter-throw']) {
      const reason = {};
      async function observe(recovered) {
        const log = [];
        class Oracle { async run(input) { await input; return this; } }
        const fn = recovered ? method : Oracle.prototype.run;
        const input = {get then() {
          log.push('get-then');
          if (completion === 'getter-throw') throw reason;
          return function (resolve, reject) {
            assert.equal(this, input); log.push('call-then');
            if (completion === 'reject') reject(reason); else resolve();
          };
        }};
        const pending = Reflect.apply(fn, receiver, [input]);
        log.push('returned');
        let result, rejected = false;
        const observed = pending.then(value => { result = value; log.push('fulfilled'); },
          error => { result = error; rejected = true; log.push('rejected'); });
        queueMicrotask(() => log.push('external-microtask'));
        await observed;
        assert.equal(rejected, completion !== 'resolve');
        assert.ok(Object.is(result, rejected ? reason : receiver));
        return log;
      }
      assert.deepEqual(await observe(true), await observe(false));
    }
  }

  const first = {}, second = {};
  let resumeFirst, resumeSecond, firstDone = false;
  const a = method.call(first, new Promise(resolve => { resumeFirst = resolve; }));
  const b = method.call(second, new Promise(resolve => { resumeSecond = resolve; }));
  a.then(() => { firstDone = true; });
  resumeSecond();
  assert.equal(await b, second);
  assert.equal(firstDone, false);
  resumeFirst();
  assert.equal(await a, first);
}
