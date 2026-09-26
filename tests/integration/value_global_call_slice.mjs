// G2 ABC-only input and independent effect trace. Not executed during pause.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';

const [tool, abc] = process.argv.slice(2);
const inspect = flag => {
  const child = spawnSync(tool, [abc, '7559', flag], { encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 });
  assert.equal(child.error, undefined);
  assert.equal(child.signal, null);
  assert.equal(child.status, 1);
  return JSON.parse(child.stdout);
};
const detached = inspect('--value-source');
assert.equal(detached.route_receipt.coverage, 'unsupported');
assert.equal(detached.source, undefined); // No proven source enclosure.
const record = inspect('--record-value-slice');
assert.equal(record.record_emitted, true);
assert.equal(record.source_complete, false);
assert.equal(record.executable, false);
assert.equal(record.enclosing_route, 'legacy');
assert.equal(record.function_segments.length, 1);
const segment = record.function_segments[0];
assert.equal(segment.route_receipt.method, 7559);
assert.equal(segment.route_receipt.stage, 'printed');
assert.equal(segment.route_receipt.coverage, 'complete');
assert.equal(segment.route_receipt.legacy_writer_used, false);
assert.equal(segment.route_receipt.identity.sha256,
  '854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e');
const bytes = Buffer.from(record.source).subarray(segment.begin, segment.end);
const source = bytes.toString();
const positions = segment.local_positions;
const spans = new Map();
for (const op of positions.operations) {
  const node = (op.kind === 'expression' ? positions.expressions : positions.statements)[op.node];
  const origin = positions.origins[node.origin];
  assert.equal(origin.kind, 'original');
  spans.set(origin.pc, bytes.subarray(node.begin, node.end).toString());
}
assert.equal(spans.get(31), 'SubscriberManager');
assert.equal(spans.get(66), 'this.id__');
assert.equal(spans.get(74), '(this.id__())');
assert.ok(spans.get(81).includes('["delete"]((this.id__()))'));
assert.ok(positions.origins.some(origin => origin.pc === 77 && origin.kind === 'elided_pure'));

const Slice = Function(`return class {\n${source}\n};`)();
const method = Slice.prototype.aboutToBeDeleted;
const Event = Object.freeze({ Base: 0, BeforeRead: 1, BeforeCall: 2, Global: 3,
  GetRead: 4, GetCall: 5, DeleteRead: 6, IdRead: 7, IdCall: 8,
  DeleteCall: 9, InternalRead: 10, InternalCall: 11 });
const previous = Object.getOwnPropertyDescriptor(globalThis, 'SubscriberManager');
try {
  for (const failedAt of [null, ...Object.values(Event)]) {
    for (const thrown of [undefined, {}, Symbol('thrown')]) {
      const trace = [];
      const event = kind => { trace.push(kind); if (kind === failedAt) throw thrown; };
      const receiver = {};
      const base = {};
      const manager = {};
      const instance = {};
      const id = {};
      const getter = (object, name, kind, value) => Object.defineProperty(object, name, {
        configurable: true, get() { event(kind); return value; },
      });
      getter(receiver, '__curBp', Event.Base, base);
      getter(base, 'aboutToBeDeleted', Event.BeforeRead, function () {
        assert.equal(this, base); event(Event.BeforeCall);
      });
      getter(globalThis, 'SubscriberManager', Event.Global, manager);
      getter(manager, 'Get', Event.GetRead, function () {
        assert.equal(this, manager); event(Event.GetCall); return instance;
      });
      getter(instance, 'delete', Event.DeleteRead, function (value) {
        assert.equal(this, instance); assert.equal(value, id); event(Event.DeleteCall);
      });
      getter(receiver, 'id__', Event.IdRead, function () {
        assert.equal(this, receiver); event(Event.IdCall); return id;
      });
      getter(receiver, 'aboutToBeDeletedInternal', Event.InternalRead, function () {
        assert.equal(this, receiver); event(Event.InternalCall);
      });
      let failed = false;
      let caught;
      try { assert.equal(Reflect.apply(method, receiver, []), undefined); }
      catch (error) { failed = true; caught = error; }
      assert.equal(failed, failedAt !== null);
      if (failed) assert.equal(caught, thrown);
      const order = Object.values(Event);
      assert.deepEqual(trace, order.slice(0, failedAt === null ? order.length : failedAt + 1));
    }
  }
} finally {
  if (previous) Object.defineProperty(globalThis, 'SubscriberManager', previous);
  else delete globalThis.SubscriberManager;
}
