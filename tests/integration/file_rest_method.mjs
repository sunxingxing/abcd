import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const fixtures = execFileSync(process.argv[2], ['--rest-json'],
  {encoding: 'utf8', timeout: 60_000}).trim().split('\n').map(JSON.parse);
assert.equal(fixtures.length, 48);
for (const fixture of fixtures) {
  assert.ok(!fixture.source.includes('INCOMPLETE'));
  const text = `export class Recovered { ${fixture.source} }`;
  const {Recovered} = await import(`data:text/javascript;base64,${Buffer.from(text).toString('base64')}`);
  const method = fixture.static ? Recovered.run : Recovered.prototype.run;
  const oracle = fixture.reassigned ? function (...args) { args = 7; return args; }
    : fixture.unused ? function (...args) { return undefined; }
    : fixture.leading ? function (first, ...args) { return args; }
    : function (...args) { return args; };
  assert.equal(method.name, 'run');
  assert.equal(method.length, fixture.leading);
  assert.throws(() => Reflect.construct(method, []), TypeError);
  const object = {}, symbol = Symbol('input');
  for (const values of [[], [undefined], [object], [1, object, symbol],
      Array.from({length: 201}, (_, i) => i === 200 ? object : i)]) {
    for (const receiver of [undefined, null, {}, Recovered]) {
      const result = Reflect.apply(method, receiver, values);
      const expected = Reflect.apply(oracle, receiver, values);
      assert.deepEqual(result, expected);
      if (Array.isArray(expected)) {
        assert.ok(Array.isArray(result));
        assert.notEqual(result, values);
        assert.notEqual(result, Reflect.apply(method, receiver, values));
        for (let i = 0; i < result.length; ++i) assert.ok(Object.is(result[i], expected[i]));
      }
    }
  }
}
console.log(`rest method source: ${fixtures.length} complete families, 24 refused protocol cases`);
