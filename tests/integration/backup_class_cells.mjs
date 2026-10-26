import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {join} from 'node:path';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

// Paired oracle for recovered methods only. Neither constructor nor platform
// lifecycle runs; the real record must remain explicitly incomplete.
const [tool, root] = process.argv.slice(2);
const receipt = JSON.parse(execFileSync(tool, [join(root,'modules.abc'),'6809','--record-source','--value-source'], {encoding:'utf8'}));
assert.equal(receipt.source_complete,true); assert.equal(receipt.legacy_writer_used, undefined);
assert.doesNotMatch(receipt.source, /INCOMPLETE|UNRECOVERED/);
// The cell's first store may print as its own initializer once no observer
// intervenes; either way DOMAIN is one module-level let binding.
assert.match(receipt.source, /let DOMAIN( = 0)?;/);
assert.ok(receipt.source.includes('hilog.info(DOMAIN,'));
const reference = stripTypeScriptTypes(readFileSync(join(root,'src/entry/src/main/ets/entrybackupability/EntryBackupAbility.ts'),'utf8'), {mode:'transform'});

async function observe(source, method, inputCase, promiseMode, failAt, mutate) {
  const events = [], failure = {}; let value;
  const event = label => { events.push(label); if (label === failAt) throw failure; };
  switch (inputCase) {
    case 0: value = undefined; break;
    case 1: value = null; break;
    case 2: value = {version:3,name:'backup'}; break;
    case 3: value = 1n; break;
    case 4: value = {}; value.self = value; break;
    case 5: value = {toJSON(key) { event(`toJSON:${key}`); return {version:7}; }}; break;
  }
  const context = vm.createContext({event,failure,promiseMode,mutate});
  vm.runInContext(`
    const NativePromise = Promise;
    const stringify = JSON.stringify;
    globalThis.logger = {get info() {
      event('info:get');
      return function(...args) {
        if (this !== logger) throw new Error('logger receiver');
        event('info:call'); globalThis.logged = args;
      };
    }};
    globalThis.JSON = {get stringify() {
      event('json:get');
      return function(value) {
        if (this !== JSON) throw new Error('JSON receiver');
        event('json:call');
        if (mutate) Object.defineProperty(logger,'info',{value(){event('info:replacement');},configurable:true});
        return stringify(value);
      };
    }};
    globalThis.Promise = {get resolve() {
      event('resolve:get');
      return function() {
        if (this !== Promise) throw new Error('Promise receiver');
        event('resolve:call');
        if (promiseMode === 0) return 7;
        if (promiseMode === 1) return NativePromise.resolve(7);
        if (promiseMode === 2) return NativePromise.reject(failure);
        return {get then() { event('then:get'); return function(resolve,reject) {
          event('then:call'); if (promiseMode === 3) resolve(7); else reject(failure);
        }; }};
      };
    }};
  `,context);
  const module = new vm.SourceTextModule(source, {context});
  const hilog = new vm.SyntheticModule(['default'], function() { this.setExport('default',context.logger); }, {context});
  const parent = new vm.SyntheticModule(['default'], function() {
    this.setExport('default',class { constructor() { assert.fail('constructor is outside this probe'); } });
  }, {context});
  await module.link(name => {
    if (name === '@ohos:hilog') return hilog;
    assert.equal(name,'@ohos:application.BackupExtensionAbility'); return parent;
  });
  await module.evaluate();
  const prototype = module.namespace.default.prototype;
  const promise = Reflect.apply(prototype[method],Object.create(prototype),[value]);
  events.push('invoked');
  try { assert.equal(await promise,undefined); events.push('fulfilled'); }
  catch (error) {
    if (error === failure) events.push('rejected:sentinel');
    else { assert.equal(error.name,'TypeError'); events.push('rejected:TypeError'); }
  }
  return {events, logged:context.logged ? Array.from(context.logged) : null};
}
let scenarios = 0;
for (const method of ['onBackup','onRestore']) for (let input = 0; input < 6; ++input)
  for (let promise = 0; promise < 5; ++promise) for (const mutate of [false,true])
    for (const fail of ['', 'info:get','info:call','json:get','json:call','resolve:get','resolve:call','then:get','then:call','toJSON:']) {
      assert.deepEqual(await observe(receipt.source,method,input,promise,fail,mutate),
        await observe(reference,method,input,promise,fail,mutate), `${method}/${input}/${promise}/${fail}/${mutate}`);
      ++scenarios;
    }
console.log(`Backup class lexical capture: ${scenarios} paired method scenarios passed; constructor/platform unverified`);
