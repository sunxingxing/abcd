import assert from 'node:assert/strict';
import {readFile, writeFile} from 'node:fs/promises';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';

const [sourcePath, outputDir] = process.argv.slice(2);
const source = await readFile(sourcePath, 'utf8');
assert.ok(source.includes('this["tabBarList"] = $acc;'));
const specifier = '"@normalized:N&&&shopping/src/main/ets/constants/HomeConstants&1.0.0"';
assert.ok(source.includes(specifier));
const fixture = join(outputDir, 'home-constants.mjs');
const modulePath = join(outputDir, 'home-model.mjs');
await writeFile(fixture, `
export const reads = [];
export const HomeConstants = {
  get TAB_BAR_TEXT_LIST() {
    reads.push('text');
    return ['Home', 'Profile'];
  },
  get TAB_BAR_ICON_LIST() {
    reads.push('icon');
    return ['home.png', 'profile.png'];
  },
  get TAB_BAR_ACTIVE_ICON_LIST() {
    reads.push('active');
    return ['home-active.png', 'profile-active.png'];
  }
};
`);
await writeFile(modulePath, source.replaceAll(specifier,
                                               '"./home-constants.mjs"'));
const {HomeModel} = await import(pathToFileURL(modulePath));
const {reads} = await import(pathToFileURL(fixture));
assert.equal(HomeModel.name, 'HomeModel');
assert.equal(HomeModel.length, 0);
assert.equal(HomeModel.prototype.initTabBarList.length, 0);
assert.equal(HomeModel.prototype.getTabBarList.length, 0);
const instance = new HomeModel();
assert.equal(Object.hasOwn(instance, 'tabBarList'), true);
const expected = [
  {text: 'Home', icon: 'home.png', activeIcon: 'home-active.png'},
  {text: 'Profile', icon: 'profile.png', activeIcon: 'profile-active.png'},
];
assert.deepEqual(instance.getTabBarList(), expected);
assert.deepEqual(reads, ['text', 'icon', 'active', 'icon', 'active']);
const first = instance.getTabBarList();
instance.initTabBarList();
assert.equal(instance.getTabBarList(), first);
assert.deepEqual(instance.getTabBarList(), [...expected, ...expected]);
assert.deepEqual(reads, [
  'text', 'icon', 'active', 'icon', 'active',
  'text', 'icon', 'active', 'icon', 'active',
]);
