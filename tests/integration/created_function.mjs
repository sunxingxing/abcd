import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const {recovered, annotated} = await import(pathToFileURL(process.argv[2]));
assert.equal(recovered.name, 'Recovered');
assert.equal(recovered.length, 1);
for (const value of [undefined, null, true, 0, -0, NaN, 42n,
                     Symbol('argument'), Object.freeze({value: 1})]) {
  assert.ok(Object.is(recovered(value), value));
}
assert.equal(recovered(), undefined);
assert.equal(Object.getPrototypeOf(new recovered()), recovered.prototype);
const replacement = Object.freeze({replacement: true});
assert.equal(new recovered(replacement), replacement);
assert.equal(annotated.length, 3);
assert.equal(annotated('first', 'second', replacement), replacement);
assert.equal(annotated.call(replacement, 1, 2, 3), 3);
assert.equal(annotated('first'), undefined);
