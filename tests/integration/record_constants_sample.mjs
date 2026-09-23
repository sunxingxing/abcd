import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const {ROUTES} = await import(pathToFileURL(process.argv[2]));
assert.equal(ROUTES.length, 2);
assert.deepEqual(ROUTES.map(route => route.title.id), [33554438, 33554440]);
assert.deepEqual(ROUTES.map(route => route.children.map(child => child.to)), [
  ['Shopping', 'ShoppingAvoid'],
  ['MiniGame', 'ShoppingFullScreen'],
]);
assert.equal(ROUTES[0].children[0].title.id, 33554437);
assert.equal(ROUTES[1].children[1].name.id, 33554443);
assert.deepEqual(ROUTES[0].title.params, []);
assert.equal(ROUTES[0].title.bundleName, 'com.example.immersive');
assert.equal(Object.getPrototypeOf(ROUTES[0].title), Object.prototype);
assert.deepEqual(Object.getOwnPropertyDescriptor(ROUTES, '0'), {
  value: ROUTES[0], writable: true, enumerable: true, configurable: true,
});
