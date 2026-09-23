import { pathToFileURL } from 'node:url';
import assert from 'node:assert/strict';

const moduleUrl = pathToFileURL(process.argv[2]).href;
const dependencyUrl = new URL('./module-import-dep.mjs', moduleUrl);
const recovered = await import(moduleUrl);
const dependency = await import(dependencyUrl);
assert.equal(recovered.read(), 1);
dependency.setValue(7);
assert.equal(recovered.read(), 7);
