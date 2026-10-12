import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

const [tool, abc, paired] = process.argv.slice(2);
const run = spawnSync(tool, [abc, '14684', '--record-source', '--value-source'], {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr);
const report = JSON.parse(run.stdout.trim());
assert.equal(report.record, 14411);
assert.equal(report.source_complete, false);
assert.equal(report.executable, false);
assert.equal(report.legacy_writer_used, undefined);
assert.match(report.source, /UNRECOVERED class definition.*method 14882; failed method 14712; parent 14656/);
const reference = stripTypeScriptTypes(readFileSync(paired, 'utf8'));
const lengths = [undefined, null, false, true, -3, -0, 0, 1, 2, 6, 7, 200, 201, 6.5, NaN, '5', 1n, Symbol('length')];

async function scenario(source, length, mode, failAt) {
  const trace = []; let step = 0;
  const token = {};
  const event = (...args) => {trace.push(args); if (++step === failAt) throw token;};
  const context = vm.createContext({ViewPU: class {}});
  const module = new vm.SourceTextModule(source, {context});
  await module.link(specifier => {
    assert.equal(specifier, '@normalized:N&&&commons/Index&1.0.0');
    return new vm.SyntheticModule(['CommonConstants', 'BreakpointType'], function() {
      this.setExport('CommonConstants', {}); this.setExport('BreakpointType', class {});
    }, {context});
  });
  await module.evaluate();
  // Invoke only the restored method/callback on a prototype receiver. The
  // constructor, omitted initialRender family, UI runtime and full record are
  // outside this independent paired-source comparison.
  const receiver = Object.create(module.namespace.Content.prototype);
  const ArrayCtor = vm.runInContext('Array', context);
  const native = ArrayCtor.from;
  Object.defineProperty(ArrayCtor, 'from', {get() {
    event('get-from');
    return function(input, callback) {
      assert.equal(this, ArrayCtor); event('from');
      assert.equal(callback.length, 2); assert.equal(callback.name, '');
      if (mode === 'native') return Reflect.apply(native, this, [input, callback]);
      const actualLength = input.length; event('length', typeof actualLength);
      if (mode === 'replay') return [callback(undefined, 0), callback(undefined, 1), callback(undefined, 200), callback(undefined, 201)];
      return [callback(undefined, {[Symbol.toPrimitive](hint) {event('index-coerce', hint); return 7;}})];
    };
  }});
  const argument = mode === 'native' ? length : {[Symbol.toPrimitive](hint) {event('length-coerce', hint); return length;}};
  let result, escaped;
  try {result = Reflect.apply(receiver.getBirdImages, receiver, [argument]);}
  catch (error) {escaped = error === token ? 'token' : error.name;}
  return {trace, escaped, result: result && JSON.parse(JSON.stringify(result))};
}
let cases = 0;
for (const length of lengths) for (const mode of ['native', 'replay', 'coerce-index'])
  for (let failAt = 0; failAt <= 8; ++failAt) {
    assert.deepEqual(await scenario(report.source, length, mode, failAt),
      await scenario(reference, length, mode, failAt)); ++cases;
  }
console.log(`${cases} paired Content method/callback scenarios passed; omitted initialRender and incomplete record retained`);
