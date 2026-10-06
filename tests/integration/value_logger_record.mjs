import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
import {verifyLoggerState} from './logger_record_state.mjs';

const [tool, abc, paired, entry = '4712', hash =
  '854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e', prefix = 'Pasteboard'] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  hash);
const run = spawnSync(tool, [abc, entry, '--record-source', '--value-source'], {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr + run.stdout);
const report = JSON.parse(run.stdout);
assert.equal(report.source_complete, true); assert.equal(report.legacy_writer_used, false);
assert.equal(report.route, 'value_tree');
// Only the independent oracle reads paired TS. No source enters the engine.
// The paired Ark TS build erases uninitialized type-only private declarations.
// Node's stripTypeScriptTypes instead preserves them as native JS fields,
// creating properties early. Apply the sample's explicit TS emission premise
// in the test-only reference before stripping types; initialized fields stay.
const pairedText = readFileSync(paired, 'utf8');
const erased = pairedText.replace(/^\s*private\s+[A-Za-z_$][\w$]*\s*:\s*[^;=\n]+;\s*$/gm, '');
assert.equal((pairedText.match(/^\s*private\s+[A-Za-z_$][\w$]*\s*:\s*[^;=\n]+;\s*$/gm) ?? []).length, 2);
// Whole-record representation permits equivalent local composition: inline the
// named class in its default construction, and retain bytecode-proven instance
// assignments instead of introducing native DefineField behavior.
assert.match(pairedText, /class\s+Logger\s*\{/);
assert.match(report.source, /export default new \(class Logger \{/);
assert.doesNotMatch(report.source, /\(\{\["Logger"\]: class/);
assert.ok(!report.source.includes('let $default'));
assert.match(report.source, /^import hilog from "@ohos:hilog";/);
assert.match(report.source, /constructor\(prefix\)\s*\{/);
for (const field of ['format', 'prefix', 'domain']) assert.match(report.source, new RegExp(`this\\.${field} =`));
assert.doesNotMatch(report.source, /INCOMPLETE|UNRECOVERED|let \$value/);
const reference = stripTypeScriptTypes(erased);
const methods = ['debug', 'info', 'warn', 'error'];
async function observe(source, method, args, mode, failure) {
  const trace = [];
  let logger;
  const hilog = {};
  for (const name of methods) Object.defineProperty(hilog, name, {get() {
    trace.push(['callee', name]);
    if (mode === 'callee-throw') throw failure;
    if (mode === 'callee-mutate') { logger.prefix = 'changed'; logger.domain = 17; }
    return function(domain, prefix, format, rest) {
      trace.push(['call', name, this === hilog, arguments.length, domain, prefix, format]);
      assert.equal(Array.isArray(rest), true);
      assert.equal(Object.getPrototypeOf(rest), vm.runInContext('Array.prototype', context));
      assert.deepEqual(Array.from(rest), args);
      assert.notEqual(rest, args);
      if (mode === 'call-throw') throw failure;
      return 'host return must be discarded';
    };
  }});
  const context = vm.createContext({});
  const root = new vm.SourceTextModule(source, {context});
  const host = new vm.SyntheticModule(['default'], function() { this.setExport('default', hilog); }, {context});
  await root.link(name => { assert.equal(name, '@ohos:hilog'); return host; });
  assert.throws(() => root.namespace.default, {name: 'ReferenceError'});
  await root.evaluate();
  assert.deepEqual(Object.keys(root.namespace), ['default']);
  logger = root.namespace.default;
  assert.equal(logger, root.namespace.default);
  assert.deepEqual(Object.keys(logger), ['format', 'prefix', 'domain']);
  assert.equal(logger.format, '%{public}s, %{public}s');
  assert.equal(logger.prefix, prefix); assert.equal(logger.domain, 65280);
  assert.equal(logger.constructor.name, 'Logger'); assert.equal(logger.constructor.length, 1);
  for (const name of methods) {
    const descriptor = Object.getOwnPropertyDescriptor(Object.getPrototypeOf(logger), name);
    assert.equal(descriptor.enumerable, false); assert.equal(descriptor.writable, true);
    assert.equal(descriptor.configurable, true);
    assert.equal(descriptor.value.name, name); assert.equal(descriptor.value.length, 0);
  }
  if (mode === 'domain-throw') Object.defineProperty(logger, 'domain', {get() {
    trace.push(['domain']); throw failure;
  }});
  try {
    const result = Reflect.apply(logger[method], logger, args);
    assert.equal(result, undefined);
    assert.equal(mode === 'callee-throw' || mode === 'call-throw' || mode === 'domain-throw', false);
  } catch (error) {
    assert.equal(error, failure); // Arbitrary thrown identity, including undefined.
    assert.ok(['callee-throw', 'call-throw', 'domain-throw'].includes(mode));
  }
  return trace;
}
let scenarios = 0;
for (const method of methods) for (const args of [[], ['one'], ['one', 'two'], [undefined, null, {}]]) {
  for (const mode of ['normal', 'callee-mutate', 'callee-throw', 'call-throw', 'domain-throw']) {
    for (const failure of [undefined, 0, {}]) {
      assert.deepEqual(await observe(report.source, method, args, mode, failure),
        await observe(reference, method, args, mode, failure));
      ++scenarios;
    }
  }
}
for (const mutate of [
  source => source.replace('this.domain = 65280', 'this.domain = 0'),
  source => source.replace('this.format = "%{public}s, %{public}s"', 'this.format = "wrong"'),
]) {
  const changed = mutate(report.source);
  assert.notEqual(changed, report.source); new vm.SourceTextModule(changed);
  await assert.rejects(observe(changed, 'debug', ['one', 'two'], 'normal', {}));
}
for (const method of methods) {
  assert.match(report.source, new RegExp(`${method}\\(\\.\\.\\.args\\)`));
  assert.match(report.source, new RegExp(`hilog\\.${method}\\(this\\.domain, this\\.prefix, this\\.format, args\\);`));
}
const state = await verifyLoggerState(report.source, reference, prefix);
console.log(JSON.stringify({basicCases: scenarios, basicMutations: 2, ...state, wholeRecordStyle: 'accepted',
  localComposition: 'named class expression in default construction; pinned instance assignments'}));
