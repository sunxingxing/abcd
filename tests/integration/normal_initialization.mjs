import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const fixtures = execFileSync(process.argv[2], ['--json'], {encoding: 'utf8', timeout: 60_000})
  .trim().split('\n').map(JSON.parse);
assert.equal(fixtures.length, 24);
let cases = 0;
for (const fixture of fixtures) {
  assert.ok(!fixture.source.includes('INCOMPLETE'));
  const recovered = vm.runInNewContext(`(${fixture.source})`);
  const trace = [];
  const object = Object.defineProperty({}, 'value', {get() { trace.push('get'); throw new Error('unexpected observation'); }});
  const proxy = new Proxy({}, {get() { trace.push('proxy'); throw new Error('unexpected observation'); }});
  const inputs = [undefined, null, false, true, 0, -0, NaN, Infinity, -Infinity, 42, 1n, Symbol('input'), object, proxy];
  for (const value of inputs) {
    // Independent language oracle: VM-private allocation/store/load machinery
    // preserves the argument, with no observable read/coercion of that object.
    const expected = (function(value) { let cell = value; return cell; })(value);
    assert.ok(Object.is(recovered(value), expected));
    assert.equal(recovered.length, 1);
    assert.deepEqual(trace, []);
    ++cases;
  }
}
console.log(`${cases} normal initialization identity and effect scenarios passed`);
