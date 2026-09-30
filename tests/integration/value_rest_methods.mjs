// Paired Logger.ts is the independent specification. Only ABC goes to the
// production CLI; the test supplies its imported module and class enclosure.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {readFileSync} from 'node:fs';

const [tool, sample] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(sample)).digest('hex'),
  '854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e');
for (const [method, name] of [[4604, 'info'], [4631, 'warn'], [4658, 'debug'], [4685, 'error']]) {
  const run = spawnSync(tool, [sample, String(method), '--family-source-draft'],
    {encoding: 'utf8', timeout: 60_000, maxBuffer: 4 * 1024 * 1024});
  assert.equal(run.error, undefined);
  assert.equal(run.status, 1, run.stderr); // A complete method is still a record fragment.
  const report = JSON.parse(run.stdout);
  assert.equal(report.family_tree_prepared, true);
  assert.equal(report.draft_prepared, true);
  assert.equal(report.draft_incomplete, false);
  assert.equal(report.source_complete, false);
  assert.equal(report.draft_root_fragment, 'class-member');
  assert.equal(report.draft_positions.length, 1);
  const recordRun = spawnSync(tool, [sample, String(method), '--record-value-slice'],
    {encoding: 'utf8', timeout: 60_000, maxBuffer: 4 * 1024 * 1024});
  assert.equal(recordRun.status, 1, recordRun.stderr);
  const record = JSON.parse(recordRun.stdout);
  assert.equal(record.record_emitted, true, JSON.stringify(record));
  assert.equal(record.source_complete, false);
  assert.equal(record.enclosing_route, 'legacy');
  assert.ok(record.source.startsWith('import hilog from \"@ohos:hilog\";'));
  assert.equal(record.function_segments.length, 1);
  const segment = record.function_segments[0];
  assert.equal(segment.route_receipt.method, method);
  assert.equal(segment.route_receipt.route, 'value_tree');
  assert.equal(segment.route_receipt.coverage, 'complete');
  assert.equal(segment.route_receipt.legacy_writer_used, false);
  assert.equal(Buffer.from(record.source).subarray(segment.begin, segment.end).toString(), report.draft_source);
  const source = report.draft_source;
  assert.ok(source.startsWith(`${name}(...args)`));
  assert.ok(!source.includes('INCOMPLETE'));
  // The native rest formal must retain the COPYRESTARGS origin as an actual
  // reference, while the original physical-frame MOVs remain elided.
  const positions = report.draft_positions[0].positions;
  const rest = positions.instruction_sources.find(entry => positions.origins[entry.origin].pc === 8);
  assert.equal(rest.kind, 'expression');
  assert.equal(Buffer.from(source).subarray(rest.range.begin, rest.range.end).toString(), 'args');
  for (const entry of positions.instruction_sources.slice(0, 4)) assert.equal(entry.kind, 'elided_pure');
  const reasons = [undefined, null, Symbol('reason'), {}];
  const values = [[], [undefined], ['text'], [1, {}, Symbol('arg')], Array.from({length: 201}, (_, i) => i)];
  for (const input of values) {
    for (const failAt of [null, 'method', 'domain', 'prefix', 'format', 'invoke']) {
      for (const reason of reasons) {
        function execute(recovered) {
          const events = [];
          let passed;
          const hilog = {
            get [name]() {
              events.push('method');
              if (failAt === 'method') throw reason;
              return function (...actual) {
                events.push('invoke');
                assert.equal(this, hilog);
                assert.equal(actual.length, 4);
                assert.deepEqual(actual.slice(0, 3), ['domain', 'prefix', 'format']);
                passed = actual[3];
                assert.ok(Array.isArray(passed));
                assert.deepEqual(passed, input);
                assert.notEqual(passed, input);
                for (let i = 0; i < passed.length; ++i) assert.ok(Object.is(passed[i], input[i]));
                if (failAt === 'invoke') throw reason;
                return {get then() { assert.fail('call result must not be awaited'); }};
              };
            }
          };
          const receiver = {};
          for (const property of ['domain', 'prefix', 'format'])
            Object.defineProperty(receiver, property, {get() {
              events.push(property);
              if (failAt === property) throw reason;
              return property;
            }});
          const Recovered = Function('hilog', `return class { ${source} };`)(hilog);
          const oracle = function (...args) { hilog[name](this.domain, this.prefix, this.format, args); };
          const fn = recovered ? Recovered.prototype[name] : oracle;
          if (recovered) assert.equal(fn.length, 0);
          let thrown = false, result;
          try { result = Reflect.apply(fn, receiver, input); }
          catch (error) { thrown = true; assert.ok(Object.is(error, reason)); }
          assert.equal(thrown, failAt !== null);
          if (!thrown) {
            assert.equal(result, undefined);
            const first = passed;
            Reflect.apply(fn, receiver, input);
            assert.notEqual(passed, first);
          }
          return events;
        }
        assert.deepEqual(execute(true), execute(false));
      }
    }
  }
}
console.log('four real Logger methods: rest identities, receivers, evaluation order and thrown identities passed');
