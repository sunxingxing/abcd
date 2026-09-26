// T03/S03/S09/A04 regression specification. Not executed during the test pause.
// Inputs are ABC-only. This independent trace oracle follows the frozen
// read@14/branch@24/read@36/write@40/return@46 sequence, not paired source.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';

const [tool, abc, methodText, hash, property] = process.argv.slice(2);
const method = Number(methodText);
const child = spawnSync(tool, [abc, methodText, '--value-source'], {
  encoding: 'utf8', maxBuffer: 4 * 1024 * 1024,
});
assert.equal(child.error, undefined);
assert.equal(child.signal, null);
assert.equal(child.status, 1); // A member fragment is not a complete record.
const report = JSON.parse(child.stdout);
assert.equal(report.route_receipt.identity.sha256, hash);
assert.equal(report.route_receipt.method, method);
assert.equal(report.route_receipt.route, 'value_tree');
assert.equal(report.route_receipt.stage, 'printed');
assert.equal(report.route_receipt.coverage, 'incomplete');
assert.equal(report.route_receipt.failure, null);
assert.equal(report.route_receipt.legacy_writer_used, false);
assert.equal(report.fragment, 'class_member');
assert.equal(report.method_slice_complete, true);
assert.equal(report.executable, false);

const positions = report.positions;
const bytes = Buffer.from(report.source, 'utf8');
assert.equal(positions.unit, 'utf8_byte');
for (const node of [...positions.expressions, ...positions.statements]) {
  assert.ok(Number.isSafeInteger(node.begin) && Number.isSafeInteger(node.end));
  assert.ok(node.begin >= 0 && node.begin < node.end && node.end <= bytes.length);
  assert.ok(node.origin >= 0 && node.origin < positions.origins.length);
}
for (const anchor of positions.anchors) {
  assert.ok(anchor.byte >= 0 && anchor.byte <= bytes.length);
}
for (const pc of [0, 2, 4, 6]) {
  assert.ok(positions.origins.some(origin => origin.pc === pc && origin.kind === 'elided_pure'));
}
for (const pc of [14, 24, 36, 40, 46]) {
  assert.ok(positions.operations.some(operation => {
    const table = operation.kind === 'expression' ? positions.expressions :
      operation.kind === 'statement' ? positions.statements : positions.anchors;
    const node = table[operation.node];
    const origin = positions.origins[node.origin];
    return origin.kind === 'original' && origin.pc === pc;
  }));
}

// The fragment must inhabit class syntax, preserving strictness and method
// non-constructibility. This fixture supplies no extra host/intrinsic premises.
const Slice = Function(`return class {\n${report.source}\n};`)();
const recovered = Slice.prototype.setInitiallyProvidedValue;
assert.equal(recovered.name, 'setInitiallyProvidedValue');
assert.equal(recovered.length, 1);
assert.throws(() => Reflect.construct(recovered, [{}]), TypeError);
const Event = Object.freeze({ Read: 0, Write: 1 });
const retained = {};
const thrownObject = {};
const cases = [
  { values: [undefined] },
  { values: [0, 17] },
  { values: [false, null] },
  { values: [null, retained] },
  { values: [retained, undefined] },
  { values: [NaN, -0] },
  { values: [true], throwRead: 0, thrown: undefined },
  { values: [true], throwRead: 0, thrown: thrownObject },
  { values: [true, false], throwRead: 1, thrown: Symbol('exception') },
  { values: [true, retained], throwWrite: true, thrown: thrownObject },
];
for (const scenario of cases) {
  const events = [];
  let reads = 0;
  const params = new Proxy({}, {
    get(target, key) {
      assert.equal(key, property);
      events.push([Event.Read, reads]);
      if (reads === scenario.throwRead) throw scenario.thrown;
      return scenario.values[reads++];
    },
  });
  const receiver = new Proxy(new Slice(), {
    set(target, key, value) {
      assert.equal(key, property);
      events.push([Event.Write, value]);
      if (scenario.throwWrite) throw scenario.thrown;
      return Reflect.set(target, key, value);
    },
  });
  let didThrow = false;
  let caught;
  let returned;
  try { returned = Reflect.apply(recovered, receiver, [params]); }
  catch (error) { didThrow = true; caught = error; }
  const expected = [[Event.Read, 0]];
  let expectedThrow = scenario.throwRead === 0;
  if (!expectedThrow && scenario.values[0] !== undefined) {
    expected.push([Event.Read, 1]);
    expectedThrow = scenario.throwRead === 1;
    if (!expectedThrow) {
      expected.push([Event.Write, scenario.values[1]]);
      expectedThrow = scenario.throwWrite === true;
    }
  }
  assert.deepEqual(events, expected);
  assert.equal(didThrow, expectedThrow);
  if (expectedThrow) assert.equal(caught, scenario.thrown);
  else assert.equal(returned, undefined);
}

// A failed strict property write must throw, not quietly return normally.
const receiver = new Proxy(new Slice(), { set() { return false; } });
assert.throws(() => Reflect.apply(recovered, receiver, [{ [property]: 1 }]), TypeError);
