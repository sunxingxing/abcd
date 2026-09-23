import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const {config, alias, routes} = await import(pathToFileURL(process.argv[2]));
assert.equal(config, alias);
assert.equal(config.answer, 42);
assert.equal(Object.getPrototypeOf(config), Object.prototype);
assert.equal(Object.getOwnPropertyDescriptor(config, '__proto__').value,
             'plain');
assert.deepEqual(Object.getOwnPropertyDescriptor(config, 'answer'), {
  value: 42, writable: true, enumerable: true, configurable: true,
});
assert.equal(routes.length, 1);
assert.equal(routes[0].id, 42);
assert.deepEqual(routes[0].params, []);
