import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

const samples = ['--receiver-argument-json', '--receiver-pair-json', '--receiver-typeof-json'].map(option =>
  JSON.parse(execFileSync(process.argv[2], [option], { encoding: 'utf8' })));
const methods = samples.map(sample => {
  const Recovered = Function(`"use strict"; return class { ${sample.source} };`)();
  return new Recovered();
});
const payloads = [undefined, null, false, 0, -0, NaN, 9n, Symbol('payload'), {}, new Proxy({}, {})];
for (const payload of payloads) {
  for (const index of [0, 2]) for (const failure of ['none', 'callee', 'argument', 'call']) {
    const events = [];
    let read = 0;
    const input = { get run() {
      if (read++ === 0) {
        events.push('callee');
        if (failure === 'callee') throw payload;
        return new Proxy(function () {}, {
          get() { assert.fail('no extra call/apply lookup'); },
          apply(target, receiver, args) {
            events.push('call');
            assert.equal(receiver, input);
            assert.equal(args.length, 1);
            assert(Object.is(args[0], index === 0 ? payload : typeof payload));
            if (failure === 'call') throw payload;
            return payload;
          },
        });
      }
      events.push('argument');
      if (failure === 'argument') throw payload;
      return payload;
    } };
    assert(Object.is(methods[index].run(input), failure === 'none'
      ? !(payload === false || payload === 0) : payload));
    const expected = ['callee', 'argument', 'call'];
    if (failure !== 'none') expected.length = expected.indexOf(failure) + 1;
    assert.deepEqual(events, expected);
  }
  for (const failure of ['none', 'callee', 'left', 'right', 'left conversion', 'right conversion', 'call']) {
    const events = [];
    const record = name => { events.push(name); if (failure === name) throw payload; };
    const left = { [Symbol.toPrimitive](hint) { assert.equal(hint, 'default'); record('left conversion'); return 2; } };
    const right = { [Symbol.toPrimitive](hint) { assert.equal(hint, 'default'); record('right conversion'); return 3; } };
    let read = 0;
    const input = { get run() {
      switch (read++) {
        case 0:
          record('callee');
          return function (value) {
            record('call'); assert.equal(this, input); assert.equal(value, '2run3'); return payload;
          };
        case 1: record('left'); return left;
        case 2: record('right'); return right;
        default: assert.fail('duplicate getter evaluation');
      }
    } };
    assert(Object.is(methods[1].run(input), failure === 'none'
      ? !(payload === false || payload === 0) : payload));
    const expected = ['callee', 'left', 'left conversion', 'right', 'right conversion', 'call'];
    if (failure !== 'none') expected.length = expected.indexOf(failure) + 1;
    assert.deepEqual(events, expected);
  }
}
// The callable check follows argument evaluation, including implicit coercion.
for (const [index, method] of methods.entries()) {
  const events = [];
  let read = 0;
  const input = { get run() {
    events.push('get');
    if (read++ === 0) return null;
    return { [Symbol.toPrimitive]() { events.push('convert'); return 1; } };
  } };
  const caught = method.run(input);
  assert(caught instanceof TypeError);
  assert.deepEqual(events, index !== 1 ? ['get', 'get'] : ['get', 'get', 'convert', 'get', 'convert']);
}

const originalPcs = [
  [0, 2, 6, 8, 10, 14, 16, 18, 22, 24, 25, 26, 27, 28],
  [0, 2, 6, 8, 10, 14, 16, 19, 22, 24, 26, 30, 33, 35, 37, 41, 43, 44, 45, 46, 47],
  [0, 2, 6, 8, 10, 14, 16, 18, 20, 24, 26, 27, 28, 29, 30],
];
for (const [index, sample] of samples.entries()) {
  const map = sample.positions;
  const source = Buffer.from(sample.source);
  const pcs = origin => {
    const entry = map.origins[origin];
    if (entry.kind === 'original') return [entry.pc];
    assert.equal(entry.kind, 'combined');
    return map.origin_parts.slice(entry.first_part, entry.first_part + entry.part_count).flatMap(pcs);
  };
  assert.equal(map.operations.length, originalPcs[index].length);
  const ranges = new Map();
  for (const placement of map.operations) {
    const range = placement.kind === 'anchor' ? map.anchors[placement.node] :
      placement.kind === 'statement' ? map.statements[placement.node] : map.expressions[placement.node];
    const pc = originalPcs[index][placement.operation];
    assert(pcs(range.origin).includes(pc));
    if (placement.kind === 'anchor') assert(range.byte <= source.length);
    else assert(range.begin < range.end && range.end <= source.length);
    ranges.set(pc, range);
  }
  const callee = ranges.get(2), left = ranges.get(10);
  const invocation = ranges.get([18, 37, 20][index]);
  assert(invocation.begin <= callee.begin && callee.end < left.begin && left.end <= invocation.end);
  assert.equal(source.subarray(callee.begin, callee.end).toString(), '$arg0.run');
  assert.equal(source.subarray(left.begin, left.end).toString(), '$arg0.run');
  if (index === 1) {
    const right = ranges.get(26), addition = ranges.get(30);
    assert(left.end < right.begin && right.end <= invocation.end);
    assert(addition.begin <= left.begin && right.end <= addition.end);
    assert.equal(source.subarray(addition.begin, addition.end).toString(), '(($arg0.run + "run") + $arg0.run)');
  }
}
