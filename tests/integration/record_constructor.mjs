import assert from 'node:assert/strict';
import {readFile, writeFile} from 'node:fs/promises';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';

const [sourcePath, outputDir] = process.argv.slice(2);
const source = await readFile(sourcePath, 'utf8');
const specifier = '"@ohos:hilog"';
assert.ok(source.includes(specifier));
const fixture = join(outputDir, 'hilog.mjs');
const modulePath = join(outputDir, 'logger.mjs');
await writeFile(fixture, `
const calls = [];
const hilog = Object.fromEntries(['debug', 'info', 'warn', 'error'].map(name => [
  name,
  function (...args) { calls.push({name, receiver: this, args}); }
]));
export {calls};
export default hilog;
`);
await writeFile(modulePath, source.replaceAll(specifier, '"./hilog.mjs"'));
const {default: logger} = await import(pathToFileURL(modulePath));
const {default: hilog, calls} = await import(pathToFileURL(fixture));
assert.equal(logger.constructor.name, 'Logger');
assert.equal(logger.constructor.length, 1);
assert.equal(logger.domain, 0xff00);
assert.equal(logger.prefix, 'Pasteboard');
assert.equal(logger.format, '%{public}s, %{public}s');
for (const name of ['debug', 'info', 'warn', 'error']) {
  assert.equal(logger[name].length, 0);
  assert.equal(logger[name](1, 'two', null), undefined);
}
assert.equal(calls.length, 4);
for (const [index, name] of ['debug', 'info', 'warn', 'error'].entries()) {
  assert.equal(calls[index].name, name);
  assert.equal(calls[index].receiver, hilog);
  assert.deepEqual(calls[index].args,
                   [0xff00, 'Pasteboard', '%{public}s, %{public}s',
                    [1, 'two', null]]);
}
logger.debug('again');
assert.notEqual(calls[0].args[3], calls[4].args[3]);
