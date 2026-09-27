import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

// Encoded Code header, try table, creation context and name pool are read from
// the fixture file. No hand-built source tree or draft printer participates.
const result = JSON.parse(execFileSync(process.argv[2], ['--exception-json'], { encoding: 'utf8' }));
const copied = JSON.parse(execFileSync(process.argv[2], ['--dead-copies-json'], { encoding: 'utf8' }));
const aliases = JSON.parse(execFileSync(process.argv[2], ['--live-copies-json'], { encoding: 'utf8' }));
const member = JSON.parse(execFileSync(process.argv[2], ['--receiver-json'], { encoding: 'utf8' }));
const stagedMember = JSON.parse(execFileSync(process.argv[2], ['--receiver-catch-json'], { encoding: 'utf8' }));
// ABI bookkeeping must add neither declarations nor reads to emitted source.
assert.equal(copied.source, result.source);
assert.equal(aliases.source, result.source);
const Recovered = Function(`"use strict"; return class { ${result.source} };`)();
const instance = new Recovered();
assert.equal(instance.run.length, 1);
const arbitrary = [undefined, null, false, -0, NaN, 19n, Symbol('payload'), {}, new Proxy({}, {})];
const Member = Function(`"use strict"; return class { ${member.source} };`)();
const receiverMethod = new Member();
const StagedMember = Function(`"use strict"; return class { ${stagedMember.source} };`)();
const stagedMethod = new StagedMember();
for (const value of arbitrary) {
  for (const failure of ['none', 'getter', 'call']) {
    const events = [];
    const input = { get run() {
      events.push('get');
      if (failure === 'getter') throw value;
      return new Proxy(function () {}, {
        get() { assert.fail('member invocation must not read call/apply'); },
        apply(target, receiver, args) {
          assert.equal(receiver, input);
          assert.deepEqual(args, [7]);
          events.push('call');
          if (failure === 'call') throw value;
          return value;
        },
      });
    } };
    const expected = failure === 'none' ? !(value === false || value === 0) : value;
    assert(Object.is(receiverMethod.run(input), expected));
    assert.deepEqual(events, failure === 'getter' ? ['get'] : ['get', 'call']);
  }
}
// Both throwing sites must expose the same original register snapshot to catch.
for (const value of arbitrary) {
  for (const failure of ['none', 'getter', 'call']) {
    const events = [];
    const input = { get run() {
      events.push('get');
      if (failure === 'getter') throw value;
      return function (arg) {
        events.push('call');
        assert.equal(this, input);
        assert.equal(arg, input);
        if (failure === 'call') throw value;
        return value;
      };
    } };
    assert(Object.is(stagedMethod.run(input),
      failure === 'none' ? !(value === false || value === 0) : input));
    assert.deepEqual(events, failure === 'getter' ? ['get'] : ['get', 'call']);
  }
}
for (const value of arbitrary) {
  for (const failure of ['none', 'getter', 'call']) {
    const events = [];
    const input = { get run() {
      events.push('get');
      if (failure === 'getter') throw value;
      return function () {
        assert.equal(this, undefined);
        events.push('call');
        if (failure === 'call') throw value;
        return value;
      };
    } };
    const actual = instance.run(input);
    const expected = failure === 'none' ? !(value === false || value === 0) : value;
    assert(Object.is(actual, expected));
    assert.deepEqual(events, failure === 'getter' ? ['get'] : ['get', 'call']);
  }
}
const map = result.positions;
assert.equal(map.unit, 'utf8_byte');
const bytes = Buffer.from(result.source, 'utf8');
const expectedPcs = [0, 2, 6, 8, 10, 11, 12, 13, 14];
assert.equal(map.operations.length, expectedPcs.length);
const pcs = (origin, mapping = map) => {
  const node = mapping.origins[origin];
  if (node.kind === 'original') return [node.pc];
  assert.equal(node.kind, 'combined');
  return mapping.origin_parts.slice(node.first_part, node.first_part + node.part_count)
    .flatMap(part => pcs(part, mapping));
};
const memberPcs = [0, 2, 6, 8, 13, 15, 17, 21, 23, 24, 25, 26, 27];
const stagedPcs = [0, 3, 5, 9, 11, 13, 17, 19, 20, 21, 22, 23, 25];
const stagedMap = stagedMember.positions;
assert.equal(stagedMap.operations.length, stagedPcs.length);
for (const placement of stagedMap.operations) {
  const range = placement.kind === 'anchor' ? stagedMap.anchors[placement.node] :
    placement.kind === 'statement' ? stagedMap.statements[placement.node] : stagedMap.expressions[placement.node];
  assert(pcs(range.origin, stagedMap).includes(stagedPcs[placement.operation]));
  if (placement.kind === 'anchor') assert(range.byte <= Buffer.byteLength(stagedMember.source));
  else assert(range.begin < range.end && range.end <= Buffer.byteLength(stagedMember.source));
}
const memberMap = member.positions;
const memberBytes = Buffer.from(member.source);
assert.equal(memberMap.operations.length, memberPcs.length);
for (const placement of memberMap.operations) {
  const range = placement.kind === 'anchor' ? memberMap.anchors[placement.node] :
    placement.kind === 'statement' ? memberMap.statements[placement.node] : memberMap.expressions[placement.node];
  assert(pcs(range.origin, memberMap).includes(memberPcs[placement.operation]));
  if (placement.kind === 'anchor') assert(range.byte <= memberBytes.length);
  else assert(range.begin < range.end && range.end <= memberBytes.length);
  if (placement.operation === 1) {
    assert.equal(placement.kind, 'expression');
    assert.equal(memberBytes.subarray(range.begin, range.end).toString(), '$arg0.run');
  }
  if (placement.operation === 6) {
    assert.equal(placement.kind, 'expression');
    assert.match(memberBytes.subarray(range.begin, range.end).toString(), /^\(\$arg0\.run\(/);
  }
}
for (const placement of map.operations) {
  const range = placement.kind === 'anchor' ? map.anchors[placement.node] :
    placement.kind === 'statement' ? map.statements[placement.node] : map.expressions[placement.node];
  if (placement.kind === 'anchor') assert(range.byte <= bytes.length);
  else assert(range.begin < range.end && range.end <= bytes.length);
  assert(pcs(range.origin).includes(expectedPcs[placement.operation]));
  if (placement.kind === 'statement')
    assert.match(bytes.subarray(range.begin, range.end).toString('utf8'), /^(if|return)\b/);
}
const copiedMap = copied.positions;
assert.equal(copiedMap.operations.length, expectedPcs.length + 3);
for (const placement of copiedMap.operations) {
  const prologue = placement.operation < 3;
  const range = placement.kind === 'anchor' ? copiedMap.anchors[placement.node] :
    placement.kind === 'statement' ? copiedMap.statements[placement.node] : copiedMap.expressions[placement.node];
  const pc = prologue ? placement.operation * 3 : expectedPcs[placement.operation - 3] + 9;
  assert(pcs(range.origin, copiedMap).includes(pc));
  if (prologue) {
    assert.equal(placement.kind, 'anchor');
    assert(range.byte <= Buffer.byteLength(copied.source));
  } else if (placement.kind === 'anchor') assert(range.byte <= Buffer.byteLength(copied.source));
  else assert(range.begin < range.end && range.end <= Buffer.byteLength(copied.source));
}
const aliasMap = aliases.positions;
assert.equal(aliasMap.operations.length, expectedPcs.length + 2);
for (const placement of aliasMap.operations) {
  const pc = placement.operation < 2 ? placement.operation * 3 : expectedPcs[placement.operation - 2] + 6;
  const range = placement.kind === 'anchor' ? aliasMap.anchors[placement.node] :
    placement.kind === 'statement' ? aliasMap.statements[placement.node] : aliasMap.expressions[placement.node];
  assert(pcs(range.origin, aliasMap).includes(pc));
  if (placement.operation < 3) assert.equal(placement.kind, 'anchor');
  if (placement.kind === 'anchor') assert(range.byte <= Buffer.byteLength(aliases.source));
  else assert(range.begin < range.end && range.end <= Buffer.byteLength(aliases.source));
}

const forwarded = JSON.parse(execFileSync(process.argv[2], ['--forwarder-json'], { encoding: 'utf8' }));
const Forwarded = Function(`"use strict"; return class { ${forwarded.source} };`)();
const forward = new Forwarded();
for (const value of arbitrary) {
  for (const throws of [false, true]) {
    let reads = 0;
    const input = { get run() { ++reads; if (throws) throw value; return value; } };
    assert(Object.is(forward.run(input), throws ? value : !(value === false || value === 0)));
    assert.equal(reads, 1);
  }
}
const forwardedPcs = [0, 2, 6, 8, 9, 11, 12, 14, 16, 18];
const forwardMap = forwarded.positions;
assert.equal(forwardMap.operations.length, forwardedPcs.length);
for (const placement of forwardMap.operations) {
  const range = placement.kind === 'anchor' ? forwardMap.anchors[placement.node] :
    placement.kind === 'statement' ? forwardMap.statements[placement.node] : forwardMap.expressions[placement.node];
  assert(pcs(range.origin, forwardMap).includes(forwardedPcs[placement.operation]));
  if (placement.kind === 'anchor') assert(range.byte <= Buffer.byteLength(forwarded.source));
  else assert(range.begin < range.end && range.end <= Buffer.byteLength(forwarded.source));
}
