import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const source = readFileSync(process.argv[2], 'utf8');
const tabList = Object.freeze({ origin: 'import' });
const dependency = new vm.SyntheticModule(['TabBarsInfo'], function () {
  this.setExport('TabBarsInfo', tabList);
});
const recovered = new vm.SourceTextModule(source);
await recovered.link((specifier) => {
  assert.equal(specifier,
    '@normalized:N&&&phone/src/main/ets/model/TabBarModel&');
  return dependency;
});
await recovered.evaluate();
const Type = recovered.namespace.site_44256_9();
assert.equal(Type.name, 'TabBarViewModel');
const instance = new Type();
assert.equal(instance.getTabList(), tabList);
