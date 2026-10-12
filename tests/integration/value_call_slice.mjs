// ABC-only call/reference trace specification. Execution remains paused.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';

const [tool, abc] = process.argv.slice(2);
const child = spawnSync(tool, [abc, '7613', '--value-source'], {
  encoding: 'utf8', maxBuffer: 4 * 1024 * 1024,
});
assert.equal(child.error, undefined);
assert.equal(child.signal, null);
assert.equal(child.status, 1); // A verified member is not a complete record.
const report = JSON.parse(child.stdout);
assert.equal(report.route_receipt.identity.sha256,
  '854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e');
assert.equal(report.route_receipt.method, 7613);
assert.equal(report.route_receipt.stage, 'printed');
assert.equal(report.route_receipt.coverage, 'complete');
assert.equal(report.route_receipt.legacy_writer_used, undefined);
assert.equal(report.method_slice_complete, true);
assert.equal(report.source_complete, false);
assert.equal(report.executable, false);
const bytes = Buffer.from(report.source);
const spans = new Map();
for (const placement of report.positions.operations) {
  const nodes = placement.kind === 'expression' ? report.positions.expressions :
    report.positions.statements;
  const node = nodes[placement.node];
  const origin = report.positions.origins[node.origin];
  assert.equal(origin.kind, 'original');
  spans.set(origin.pc, bytes.subarray(node.begin, node.end).toString());
}
assert.equal(spans.get(14), 'this.__curBp');
assert.ok(spans.get(22).endsWith('.purgeDependencyOnElmtId'));
assert.ok(spans.get(34).includes('.purgeDependencyOnElmtId(rmElmtId)'));
assert.equal(spans.get(39), 'return (void 0);');

const Slice = Function(`return class {\n${report.source}\n};`)();
const recovered = Slice.prototype.purgeVariableDependenciesOnElmtId;
assert.equal(recovered.length, 1);
assert.throws(() => Reflect.construct(recovered, [0]), TypeError);
const Event = Object.freeze({ Base: 0, Callee: 1, Invoke: 2 });
const thrownValues = [undefined, null, Symbol('exception'), {}];
for (const failure of [null, Event.Base, Event.Callee, Event.Invoke]) {
  for (const thrown of thrownValues) {
    const events = [];
    const argument = {};
    const value = {};
    const receiver = {};
    const method = function (actual) {
      events.push(Event.Invoke);
      assert.equal(this, value);
      assert.equal(actual, argument);
      if (failure === Event.Invoke) throw thrown;
      return Symbol('ignored return');
    };
    Object.defineProperty(value, 'purgeDependencyOnElmtId', { get() {
      events.push(Event.Callee);
      if (failure === Event.Callee) throw thrown;
      return method;
    } });
    Object.defineProperty(receiver, '__curBp', { get() {
      events.push(Event.Base);
      if (failure === Event.Base) throw thrown;
      return value;
    } });
    let failed = false;
    let caught;
    let returned;
    try { returned = Reflect.apply(recovered, receiver, [argument]); }
    catch (error) { failed = true; caught = error; }
    assert.equal(failed, failure !== null);
    if (failed) assert.equal(caught, thrown);
    else assert.equal(returned, undefined);
    assert.deepEqual(events, [Event.Base, Event.Callee, Event.Invoke].slice(0,
      failure === null ? 3 : failure + 1));
  }
}
assert.throws(() => Reflect.apply(recovered, { __curBp: null }, [0]), TypeError);
assert.throws(() => Reflect.apply(recovered,
  { __curBp: { purgeDependencyOnElmtId: 1 } }, [0]), TypeError);
