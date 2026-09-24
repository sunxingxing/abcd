import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const {Backup, events, logger} =
  await import(pathToFileURL(process.argv[2]));
const backup = new Backup();
assert.equal(Backup.prototype.onBackup.length, 0);
assert.equal(Backup.prototype.onRestore.length, 1);
assert.ok(backup.onBackup() instanceof Promise);
await backup.onBackup();
assert.deepEqual(events.splice(0), [
  'get', [logger, 0, 'testTag', 'onBackup ok'],
  'get', [logger, 0, 'testTag', 'onBackup ok'],
]);
await backup.onRestore({toJSON() {
  events.push('json');
  return {ok: 1};
}});
assert.deepEqual(events.splice(0), [
  'get', 'json', [logger, 0, 'testTag',
                  'onRestore ok %{public}s', '{"ok":1}'],
]);
const cyclic = {};
cyclic.self = cyclic;
await assert.rejects(backup.onRestore(cyclic), TypeError);
assert.deepEqual(events.splice(0), ['get']);
