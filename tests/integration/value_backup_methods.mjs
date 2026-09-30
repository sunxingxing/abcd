import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {readFileSync} from 'node:fs';

// Independent paired EntryBackupAbility oracle. The production CLI receives
// only the identity-checked ABC; no paired source or source map is an input.
const [tool, sample, methodText, sha256] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(sample)).digest('hex'), sha256);
const inspect = flag => {
  const run = spawnSync(tool, [sample, methodText, flag], {encoding: 'utf8', timeout: 60_000});
  assert.equal(run.error, undefined);
  assert.equal(run.status, 1, run.stderr);
  return JSON.parse(run.stdout);
};
const report = inspect('--family-source-draft');
assert.equal(report.draft_prepared, true, JSON.stringify(report));
assert.equal(report.draft_incomplete, false);
assert.equal(report.family_tree_prepared, true);
assert.equal(report.source_complete, false); // The complete method is a fragment.
const source = report.draft_source;
assert.ok(source.startsWith('async onRestore(bundleVersion)'));
assert.match(source, /\(?JSON\)?\.stringify\(bundleVersion\)/);
const Recovered = Function('hilog', `return class { ${source} };`);
const descriptor = Object.getOwnPropertyDescriptor(globalThis, 'JSON');
const reasons = [undefined, null, Symbol('reason'), {}];
for (const failAt of [null, 'method', 'global', 'stringify', 'serialize', 'invoke']) {
  for (const reason of reasons) {
    async function execute(recovered) {
      const events = [];
      const bundle = {};
      const serialized = {get then() { assert.fail('JSON result must not be awaited'); }};
      const json = {get stringify() {
        events.push('stringify');
        if (failAt === 'stringify') throw reason;
        return function (value) {
          events.push('serialize');
          assert.equal(this, json);
          assert.equal(value, bundle);
          if (failAt === 'serialize') throw reason;
          // Rebinding the global after GetValue must not change this call's
          // receiver or cause an extra global lookup at invocation time.
          Object.defineProperty(globalThis, 'JSON', {configurable: true, value: {}});
          return serialized;
        };
      }};
      const hilog = {get info() {
        events.push('method');
        if (failAt === 'method') throw reason;
        return function (...args) {
          events.push('invoke');
          assert.equal(this, hilog);
          assert.deepEqual(args.slice(0, 3), [0, 'testTag', 'onRestore ok %{public}s']);
          assert.equal(args[3], serialized);
          if (failAt === 'invoke') throw reason;
          return {get then() { assert.fail('hilog result must not be awaited'); }};
        };
      }};
      Object.defineProperty(globalThis, 'JSON', {configurable: true, get() {
        events.push('global');
        if (failAt === 'global') throw reason;
        return json;
      }});
      try {
        const oracle = async function (bundleVersion) {
          hilog.info(0, 'testTag', 'onRestore ok %{public}s', JSON.stringify(bundleVersion));
        };
        const method = recovered ? Recovered(hilog).prototype.onRestore : oracle;
        assert.equal(method.length, 1);
        const pending = Reflect.apply(method, {}, [bundle]);
        events.push('returned');
        let failed = false, result;
        await pending.then(value => { result = value; }, error => { failed = true; result = error; });
        assert.equal(failed, failAt !== null);
        assert.ok(Object.is(result, failed ? reason : undefined));
      } finally { Object.defineProperty(globalThis, 'JSON', descriptor); }
      return events;
    }
    assert.deepEqual(await execute(true), await execute(false));
  }
}
console.log('onRestore: nested call receivers, global lookup, effects and rejection identity passed');
