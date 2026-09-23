import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const events = [];
const failure = new Error('hilog failed');
let throwOnInfo = false;
const hilog = {
  get info() {
    events.push('hilog.get');
    return function(...args) {
      events.push('hilog.call');
      assert.equal(this, hilog);
      if (throwOnInfo) throw failure;
      events.push(args);
    };
  },
};
const json = {
  get stringify() {
    events.push('json.get');
    return function(value) {
      events.push('json.call');
      assert.equal(this, json);
      assert.equal(value, version);
      return 'encoded-version';
    };
  },
};
const context = vm.createContext({JSON: json});
class BackupExtensionAbility {
  constructor(...args) { this.argumentsSeen = args; }
}
const dependencies = new Map([
  ['@ohos:hilog', new vm.SyntheticModule(['default'], function() {
    this.setExport('default', hilog);
  }, {context})],
  ['@ohos:application.BackupExtensionAbility',
   new vm.SyntheticModule(['default'], function() {
     this.setExport('default', BackupExtensionAbility);
   }, {context})],
]);
const source = readFileSync(process.argv[2], 'utf8');
const recovered = new vm.SourceTextModule(source, {context});
await recovered.link((specifier) => {
  assert(dependencies.has(specifier));
  return dependencies.get(specifier);
});
await recovered.evaluate();
const Type = recovered.namespace.default;
assert.equal(Type.name, 'EntryBackupAbility');
assert.equal(Type.length, 0);
assert.equal(Object.getPrototypeOf(Type), BackupExtensionAbility);
const instance = new Type(1, 'second');
assert(instance instanceof BackupExtensionAbility);
assert.deepEqual(instance.argumentsSeen, [1, 'second']);
assert.equal(instance.onBackup.name, 'onBackup');
assert.equal(instance.onBackup.length, 0);
assert.equal(instance.onRestore.name, 'onRestore');
assert.equal(instance.onRestore.length, 1);
assert.equal(await instance.onBackup(), undefined);
assert.deepEqual(events, [
  'hilog.get', 'hilog.call', [0, 'testTag', 'onBackup ok'],
]);
events.length = 0;
const version = Object.freeze({major: 7});
assert.equal(await instance.onRestore(version), undefined);
assert.deepEqual(events, [
  'hilog.get', 'json.get', 'json.call', 'hilog.call',
  [0, 'testTag', 'onRestore ok %{public}s', 'encoded-version'],
]);
events.length = 0;
throwOnInfo = true;
await assert.rejects(instance.onBackup(), error => error === failure);
assert.deepEqual(events, ['hilog.get', 'hilog.call']);
