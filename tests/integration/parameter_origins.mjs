import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join, dirname} from 'node:path';
import {tmpdir} from 'node:os';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
import {verifyNavMap} from './navmap_state.mjs';

const [tool, abc, paired] = process.argv.slice(2);
const temporary = mkdtempSync(join(tmpdir(), 'abcd-parameter-origins-'));
let generated, generatedIndex;
try {
  const output = join(temporary, 'output');
  execFileSync(tool, [abc, output, '--value-source', '--language-source'],
    {timeout: 60_000});
  const manifest = JSON.parse(readFileSync(join(output, 'manifest.json')));
  const record = manifest.records.find(record => record.entity === 14411);
  assert.equal(record.source_complete, true);
  assert.equal(record.legacy_writer_used, undefined);
  generated = readFileSync(join(output, record.source), 'utf8');
  const index = manifest.records.find(record => record.entity === 9572);
  assert.equal(index.source_complete, true); assert.equal(index.legacy_writer_used, undefined);
  generatedIndex = readFileSync(join(output, index.source), 'utf8');
} finally { rmSync(temporary, {recursive: true}); }
// The paired source is evaluation input only, read after ABC-only generation.
// This executes a recovered method; platform UI/constructor behavior is separate.
const reference = stripTypeScriptTypes(readFileSync(paired, 'utf8'));
async function load(source) {
  const context = vm.createContext({ViewPU: class {}});
  const module = new vm.SourceTextModule(source, {context});
  await module.link(() => new vm.SyntheticModule(['BreakpointType', 'CommonConstants'], function() {
    this.setExport('BreakpointType', class {}); this.setExport('CommonConstants', {});
  }, {context}));
  await module.evaluate();
  return module.namespace.Content.prototype.getBirdImages;
}
const actual = await load(generated), expected = await load(reference);
assert.equal(actual.length, 1);
assert.match(actual.toString(), /getBirdImages\(count\)/);
assert.doesNotMatch(actual.toString(), /\$arg\d/);
function observe(fn, value, coercion = false, failAt = 0) {
  const trace = [], failure = Symbol('failure');
  const event = label => { trace.push(label); if (trace.length === failAt) throw failure; };
  const input = coercion ? {get [Symbol.toPrimitive]() {
    event('coercion.get');
    return function(hint) { assert.equal(this, input); event(hint); return value; };
  }} : value;
  try {
    const result = fn.call({}, input);
    return {result: JSON.parse(JSON.stringify(result)), trace,
      distinct: new Set(result).size === result.length};
  } catch (error) {
    return error === failure ? {originalException: true, trace} : {error: error.name, trace};
  }
}
const values = [undefined, null, false, true, 0, -0, -1, 1, 2.9, 6, 7, 200, 201, 402, 804,
  NaN, '', '7', 'bad', 0n, 2n, Symbol('count')];
let cases = 0;
for (const value of values) for (const coercion of [false, true]) for (const failAt of [0, 1, 2, 3]) {
  assert.deepEqual(observe(actual, value, coercion, failAt), observe(expected, value, coercion, failAt));
  ++cases;
}
const broken = generated.replace('length: count', 'length: 0');
assert.notEqual(broken, generated);
const mutated = await load(broken);
assert.notDeepEqual(observe(mutated, 7), observe(expected, 7));
console.log(`${cases} paired named-signature length/coercion/exception/object-identity scenarios passed; dropped count mutation detected`);

// Export first, then read the independent reference. Exercise the real class
// method from the complete generated ESM record, including its import bindings.
const pairedIndex = readFileSync(join(dirname(abc),
  'Immersive_src/default/src/main/ets/pages/Index.ts'), 'utf8');
const begin = pairedIndex.indexOf('    NavPageMap(name: string, parent = null) {');
const end = pairedIndex.indexOf('    initialRender()', begin);
assert.ok(begin >= 0 && end > begin);
const referenceNavMap = pairedIndex.slice(begin, end).replace('name: string', 'name');
assert.match(generatedIndex, /NavPageMap\(name, parent = null\)/);
const routeCases = await verifyNavMap(async (context, globals) => {
  const factories = [];
  context.registerNamedRoute = function(factory, name, metadata) {
    assert.equal(typeof factory, 'function'); assert.equal(name, '');
    assert.equal(metadata.pagePath, 'pages/Index'); factories.push(factory);
  };
  context.NavPathStack = class {};
  context.ObservedPropertyObjectPU = class {
    constructor(value, owner, name) { this.value = value; assert.ok(owner instanceof globals.ViewPU); assert.equal(name, 'navPageInfos'); }
    get() { return this.value; }
    set(value) { this.value = value; }
  };
  globals.ViewPU.prototype.addProvidedVar = function(name, cell, mutable) {
    assert.equal(name, 'navPageInfos'); assert.equal(cell, this.__navPageInfos); assert.equal(mutable, false);
  };
  globals.ViewPU.prototype.createStorageLink = function(name, value, alias) {
    assert.equal(name, 'windowUtil'); assert.equal(value, undefined); assert.equal(alias, name);
    return {get() {return undefined;}};
  };
  const module = new vm.SourceTextModule(generatedIndex, {context});
  await module.link(specifier => {
    const binding = specifier.includes('/Constants&') ? 'ROUTES' :
      ['ShoppingFullScreen', 'ShoppingAvoid', 'MiniGame', 'Shopping'].find(name => specifier.endsWith('/' + name + '&'));
    assert.ok(binding, specifier);
    return new vm.SyntheticModule([binding], function() {
      this.setExport(binding, binding === 'ROUTES' ? [] : globals[binding]);
    }, {context});
  });
  await module.evaluate();
  assert.equal(factories.length, 1);
  const instance = factories[0]();
  assert.ok(instance instanceof globals.ViewPU);
  return Object.getPrototypeOf(instance).NavPageMap;
}, referenceNavMap);
console.log(`${routeCases} actual-record named/default-signature route scenarios passed`);
