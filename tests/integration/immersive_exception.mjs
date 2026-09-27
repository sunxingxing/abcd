import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { SourceTextModule, SyntheticModule } from 'node:vm';

// Explicitly selected real ABC, no evaluation source or source map input.
const digest = createHash('sha256').update(readFileSync(process.argv[3])).digest('hex');
assert.equal(digest, '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a');
const result = JSON.parse(execFileSync(process.argv[2], [process.argv[3], '8284', '--emit'], { encoding: 'utf8' }));
assert.equal(result.fragment_complete, true);
assert.equal(result.record_complete, false);
const entry = new SourceTextModule(`${result.imports}
export default class { ${result.source} }
`);
const logger = new SourceTextModule('let log; export { log as default }; export function set(value) { log = value; }');
const unused = new Set(['@ohos:app.ability.AbilityConstant', '@ohos:display', '@ohos:window', '@ohos:sensor']);
await entry.link(specifier => {
  if (specifier === '@ohos:hilog') return logger;
  assert(unused.has(specifier));
  return new SyntheticModule(['default'], function () { this.setExport('default', {}); });
});
await entry.evaluate();
const recovered = entry.namespace.default.prototype.release;
// Oracle from the decoded operations: mainWindow/off, then on failure a live
// hilog read/error lookup, string additions with code and message, then call.
// This test oracle is never an engine input or a production special case.
function expected(receiver, log) {
  try { receiver.mainWindow.off('windowStatusChange'); }
  catch (error) { log.error(0, 'TestLog', 'Failed to off. Code: ' + error.code + ', message: ' + error.message + ''); }
}
const arbitrary = [undefined, null, false, -0, NaN, 7n, Symbol('payload'), {}, new Proxy({}, {})];
const Fault = Object.freeze({
  None: Symbol('none'), Window: Symbol('window'), OffGet: Symbol('off-get'),
  OffCall: Symbol('off-call'), LogGet: Symbol('log-get'), Code: Symbol('code'),
  CodeCoerce: Symbol('code-coerce'), Message: Symbol('message'),
  MessageCoerce: Symbol('message-coerce'), LogCall: Symbol('log-call'),
  LogNotCallable: Symbol('log-not-callable'),
});
function invoke(fn, fault, thrown) {
  const effects = [];
  const mark = phase => { effects.push(phase); if (fault === phase) throw thrown; };
  const error = {
    get code() { mark(Fault.Code); return { [Symbol.toPrimitive](hint) { assert.equal(hint, 'default'); mark(Fault.CodeCoerce); return 9; } }; },
    get message() { mark(Fault.Message); return { [Symbol.toPrimitive](hint) { assert.equal(hint, 'default'); mark(Fault.MessageCoerce); return 'denied'; } }; },
  };
  const window = { get off() {
    mark(Fault.OffGet);
    return new Proxy(function () {}, {
      get() { assert.fail('no call/apply lookup'); },
      apply(target, receiver, args) {
        assert.equal(receiver, window); assert.deepEqual(args, ['windowStatusChange']);
        mark(Fault.OffCall);
        if (fault !== Fault.None) throw error;
        return thrown;
      },
    });
  } };
  const receiver = { get mainWindow() { mark(Fault.Window); return window; } };
  const log = { get error() {
    mark(Fault.LogGet);
    if (fault === Fault.LogNotCallable) return null;
    return new Proxy(function () {}, {
      get() { assert.fail('no call/apply lookup'); },
      apply(target, receiver, args) {
        assert.equal(receiver, log); effects.push(['log-args', ...args]);
        mark(Fault.LogCall); return thrown;
      },
    });
  } };
  logger.namespace.set(log);
  try { return { effects, returned: fn(receiver, log) }; }
  catch (failure) { return { effects, thrown: failure }; }
}
for (const fault of Object.values(Fault)) {
  for (const value of arbitrary) {
    const oracle = invoke(expected, fault, value);
    const actual = invoke(receiver => Reflect.apply(recovered, receiver, []), fault, value);
    assert.deepEqual(actual.effects, oracle.effects, fault.description);
    assert.equal('thrown' in actual, 'thrown' in oracle, fault.description);
    if ('thrown' in oracle) {
      if (oracle.thrown === value) assert(Object.is(actual.thrown, value), fault.description);
      else assert.equal(actual.thrown.constructor, oracle.thrown.constructor, fault.description);
    } else assert(Object.is(actual.returned, oracle.returned), fault.description);
  }
}
const map = result.positions;
assert.deepEqual(map.module_binding, { owner: 0, record: 7618 });
const expectedPcs = [0, 2, 4, 6, 8, 10, 12, 16, 18, 20, 24, 26, 29, 31, 33, 37,
  39, 41, 43, 45, 47, 51, 53, 55, 59, 61, 66, 68, 71, 73, 76, 78, 80, 82,
  84, 88, 91, 93, 96, 99, 101, 103, 105, 107, 111, 114, 116, 119, 122, 124, 126, 132, 133];
const bytes = Buffer.from(result.source);
function pcs(id) {
  const node = map.origins[id];
  if ('pc' in node) return [node.pc];
  assert.equal(node.kind, 'combined');
  return map.origin_parts.slice(node.first_part, node.first_part + node.part_count).flatMap(pcs);
}
assert.equal(map.operations.length, expectedPcs.length);
for (const placement of map.operations) {
  const range = placement.kind === 'anchor' ? map.anchors[placement.node] :
    placement.kind === 'statement' ? map.statements[placement.node] : map.expressions[placement.node];
  assert(pcs(range.origin).includes(expectedPcs[placement.operation]));
  if (placement.kind === 'anchor') assert(range.byte <= bytes.length);
  else assert(range.begin < range.end && range.end <= bytes.length);
}
