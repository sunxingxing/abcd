import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';

const [engine, abc, paired] = process.argv.slice(2);
// The engine sees ABC alone. The paired constructor body is an independent
// oracle; its type-only declarations do not represent runtime field writes.
const report = JSON.parse(execFileSync(engine, [abc, '20678', '--record-source', '--value-source'], { encoding: 'utf8' }));
assert.equal(report.source_complete, true);
assert.equal(report.legacy_writer_used, undefined);
assert.match(report.source, /item\?\.reviewCnt/);
assert.match(report.source, /item\?\.subContent/);
assert.doesNotMatch(report.source, /item !== null|item !== \(void 0\)/);
const generated = await import(`data:text/javascript;base64,${Buffer.from(report.source).toString('base64')}`);
assert.deepEqual(Object.keys(generated), ['CommentItem']);
const text = readFileSync(paired, 'utf8');
const start = text.indexOf('constructor(item: CommentItemInterface) {');
assert(start >= 0);
const open = text.indexOf('{', start);
let depth = 1, end = open + 1;
for (; depth && end < text.length; ++end) {
  if (text[end] === '{') ++depth;
  else if (text[end] === '}') --depth;
}
assert.equal(depth, 0);
const Reference = Function(`return class CommentItem { constructor(item) ${text.slice(open, end)} }`)();
assert.equal(generated.CommentItem.name, Reference.name);
assert.equal(generated.CommentItem.length, Reference.length);
const fields = ['icon', 'name', 'favorCount', 'content', 'timePosition', 'reviewCnt', 'subContent'];
const token = {}, thrown = {}, object = {}, symbol = Symbol('value');
const values = [undefined, null, false, true, 0, -0, NaN, '', 'text', object, symbol];
let scenarios = 0;
function run(Type, review, sub, fail, setters) {
  const events = [], stored = [];
  let reads = 0, receiver;
  const item = Object.fromEntries(fields.map(key => [key, token]));
  item.reviewCnt = review; item.subContent = sub;
  for (const key of fields) {
    const value = item[key];
    let accesses = 0;
    Object.defineProperty(item, key, { get() {
      assert.equal(this, item);
      events.push(`read:${key}`);
      if (++reads === fail) throw thrown;
      return ++accesses === 1 ? value : token;
    }});
    if (setters) Object.defineProperty(Type.prototype, key, { configurable: true, set(value) {
      receiver ??= this; assert.equal(this, receiver);
      events.push(`write:${key}`); stored.push([key, value]);
    }});
  }
  try {
    const result = new Type(item);
    assert(result instanceof Type);
    if (setters) {
      assert.equal(result, receiver);
      for (const [, value] of stored) assert.equal(value, token);
    } else {
      for (const key of fields.slice(0, 5)) assert.equal(result[key], token);
      for (const [key, value] of [['reviewCnt', review], ['subContent', sub]]) {
        assert.equal(Object.hasOwn(result, key), Boolean(value));
        if (value) assert.equal(result[key], token);
      }
    }
    return { events, stored, descriptors: Object.getOwnPropertyDescriptors(result) };
  } catch (error) {
    assert.equal(error, thrown);
    return { events, stored, error };
  } finally {
    for (const key of fields) if (setters) delete Type.prototype[key];
  }
}
for (const review of values) for (const sub of values) for (let fail = 0; fail <= 9; ++fail) {
  for (const setters of [false, true]) {
    assert.deepEqual(run(generated.CommentItem, review, sub, fail, setters), run(Reference, review, sub, fail, setters));
    ++scenarios;
  }
}
for (const item of [null, undefined]) {
  assert.throws(() => new generated.CommentItem(item), TypeError);
  assert.throws(() => new Reference(item), TypeError);
  ++scenarios;
}
console.log(`CommentItem complete module: ${scenarios} paired constructor scenarios and optional-access representation passed`);
