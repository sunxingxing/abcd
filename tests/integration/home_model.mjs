import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const source = readFileSync(process.argv[2], 'utf8');
const constants = Object.freeze({
  TAB_BAR_TEXT_LIST: ['Home', 'Profile'],
  TAB_BAR_ICON_LIST: ['home.png', 'profile.png'],
  TAB_BAR_ACTIVE_ICON_LIST: ['home-active.png', 'profile-active.png'],
});
const dependency = new vm.SyntheticModule(['HomeConstants'], function() {
  this.setExport('HomeConstants', constants);
});
const recovered = new vm.SourceTextModule(source);
await recovered.link((specifier) => {
  assert.equal(specifier,
    '@normalized:N&&&shopping/src/main/ets/constants/HomeConstants&1.0.0');
  return dependency;
});
await recovered.evaluate();
const Type = recovered.namespace.site_16638_9();
assert.equal(Type.name, 'HomeModel');
const instance = new Type();
const expected = [
  {text: 'Home', icon: 'home.png', activeIcon: 'home-active.png'},
  {text: 'Profile', icon: 'profile.png', activeIcon: 'profile-active.png'},
];
assert.deepEqual(Array.from(instance.getTabBarList(), item => ({...item})),
                 expected);
assert.equal(Object.hasOwn(instance, 'tabBarList'), true);
instance.initTabBarList();
assert.deepEqual(Array.from(instance.getTabBarList(), item => ({...item})),
                 [...expected, ...expected]);
