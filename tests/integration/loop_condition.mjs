import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {parseJavaScript} from './class_reference.mjs';
const rows = execFileSync(process.argv[2], ['--emit'], {encoding: 'utf8', maxBuffer: 32_000_000}).trim().split('\n').map(JSON.parse);
assert.equal(rows.length, 132);
let comparisons = 0, mutations = 0;
const failure = Symbol('failure');
for (const row of rows) {
  const generated = Function('return (' + row.source + ')')();
  const parsed = parseJavaScript('(' + row.source + ')');
  const loop = parsed.body[0].expression.body.body.find(node => node.type === 'WhileStatement');
  assert.equal(loop.type, 'WhileStatement');
  assert.equal(loop.test.type === 'Literal' && loop.test.value === true, row.form === 4);
  let reference = (prefix, condition, exit, body, choice) => {
    while (true) {
      for (let i = 0; i < row.count; ++i) prefix();
      if (!condition()) {
        if (row.form === 2 || (row.form === 3 && !choice())) exit();
        break;
      }
      if (row.form === 5) { try {body(); continue;} finally {exit();} }
      body();
    }
  };
  if (row.form === 4) reference = (prefix, condition, exit, body) => {
    while (true) {
      for (let i = 0; i < row.count; ++i) prefix();
      if (!(condition(), local)) break;
      let local;
      body();
    }
  };
  if (row.form === 6) reference = async (prefix, condition, exit, body) => {
    while (true) {
      for (let i = 0; i < row.count; ++i) prefix();
      if (!await condition()) break;
      body();
    }
  };
  if (row.form === 7) reference = function*(prefix, condition, exit, body) {
    while (true) {
      for (let i = 0; i < row.count; ++i) prefix();
      if (!(yield condition())) break;
      body();
    }
  };
  if (row.decoded) {
    const original = reference;
    reference = (prefix, condition, body) => original(prefix, condition, undefined, body);
  }
  const observe = async (fn, stop, choice, throwAt, truth = {}, falsy = 0) => {
    const events = []; let iteration = 0; const requests = [];
    const event = name => {
      events.push(name); assert.ok(events.length <= (row.count + 4) * 5);
      if (events.length === throwAt) throw failure;
    };
    try {
      const callbacks = [function() {assert.equal(this, undefined); event('prefix');}, function() {
        assert.equal(this, undefined); event('condition');
        const result = iteration++ < stop ? truth : falsy;
        if (row.form === 6) {
          queueMicrotask(() => events.push('condition-job'));
          return Promise.resolve(result);
        }
        return result;
      }, function() {assert.equal(this, undefined); event('exit');}, function() {
        assert.equal(this, undefined); event('body');
      }, function() {assert.equal(this, undefined); event('choice'); return choice;}];
      let value = row.decoded ? fn(callbacks[0], callbacks[1], callbacks[3]) : fn(...callbacks);
      if (row.form === 7) {
        const generator = value; let result = generator.next();
        while (!result.done) {
          requests.push({truthy: Boolean(result.value), done: result.done});
          result = generator.next(result.value);
        }
        requests.push(result); value = result.value;
      }
      if (row.form === 6) value = await value;
      return {events, requests, value};
    } catch (error) {
      if (row.form === 4 && error instanceof ReferenceError) return {events, requests, thrown: 'ReferenceError'};
      assert.equal(error, failure); return {events, requests, thrown: error};
    }
  };
  for (const stop of [0, 1, 3]) for (const choice of [false, true]) {
    const normal = await observe(reference, stop, choice, Infinity);
    assert.deepEqual(await observe(generated, stop, choice, Infinity), normal); ++comparisons;
    const anchors = [...new Set([1, row.count, row.count + 1, row.count + 2, normal.events.length - 1, normal.events.length].filter(index => index > 0))];
    for (const at of anchors) {
      assert.deepEqual(await observe(generated, stop, choice, at), await observe(reference, stop, choice, at)); ++comparisons;
    }
  }
  if (!row.decoded && !row.reversed && row.count === 0 && row.form === 0) {
    const object = {[Symbol.toPrimitive]() {assert.fail('ToBoolean must not convert objects');}};
    for (const truth of [true, 1, -1, 1n, Symbol('truth'), 'text', object, function() {}])
      for (const falsy of [false, undefined, null, 0, -0, NaN, '']) {
        assert.deepEqual(await observe(generated, 1, true, Infinity, truth, falsy),
          await observe(reference, 1, true, Infinity, truth, falsy)); ++comparisons;
      }
  }
  if (!row.decoded && !row.reversed && row.count <= 1 && row.form !== 4) {
    const test = parsed.body[0].expression.body.body.find(node => node.type === 'WhileStatement').test;
    // Acorn parsed an extra outer pair above, so adjust its byte offsets.
    const changed = row.source.slice(0, test.start - 1) + 'false' + row.source.slice(test.end - 1);
    parseJavaScript(changed);
    const bad = Function('return (' + changed + ')')();
    assert.notDeepEqual(await observe(bad, 1, true, Infinity), await observe(reference, 1, true, Infinity)); ++mutations;
  }

}
console.log(JSON.stringify({trees: rows.length, comparisons, mutations, platformExecution: false}));
