import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {inspect, isDeepStrictEqual} from 'node:util';
import {exerciseProgressbar} from './progressbar_record_state.mjs';

const input = JSON.parse(readFileSync(0, 'utf8')), reports = [];
for (const config of input.cases) {
  const expected = await exerciseProgressbar(input.reference, config);
  const actual = await exerciseProgressbar(input.sources, config);
  if (input.mutation) {
    if (input.identityOnly) {
      const {identities: actualGraph, ...oldActual} = actual;
      const {identities: expectedGraph, ...oldExpected} = expected;
      assert.deepEqual(oldActual, oldExpected, 'original trace/value checks miss this mutation');
      assert.notDeepEqual(actualGraph, expectedGraph, 'new graph must expose the identity difference');
    }
    assert.equal(isDeepStrictEqual(actual, expected), false, input.mutation);
  }
  else if (!isDeepStrictEqual(actual, expected)) {
    const key = Object.keys(expected).find(key => !isDeepStrictEqual(expected[key], actual[key]));
    throw new Error('progressbar difference ' + key + ' ' + JSON.stringify(config) + '\n' +
      inspect({actual: actual[key], expected: expected[key]}, {depth: 8, maxArrayLength: 45, maxStringLength: 200}));
  }
  if (!config.failAt && !config.noContext && !input.mutation) {
    assert.equal(actual.diagnostics, 0);
    assert.ok(actual.modules.every(([, status]) => status === 'evaluated'));
    assert.deepEqual(actual.clicks.slice(0, 2), ['PasteButton', 'Button']);
    assert.equal(actual.owners[1][0], 'ProgressBar');
    if (!config.reject) assert.ok(actual.platform.completions.every(([status]) => status === 'fulfilled'));
  }
  const boundaries = new Map();
  for (const [ordinal, name, key] of actual.sites)
    if ((name.startsWith('platform.') || name.endsWith('.call') || name === 'ui.call') && !boundaries.has(name + ':' + key))
      boundaries.set(name + ':' + key, ordinal);
  reports.push({events: actual.events, failed: actual.failed, modules: actual.modules,
    ...(input.inventory ? {anchors: [...boundaries.values()], clicks: actual.clicks,
      cells: actual.cells, platform: actual.platform} : {})});
}
console.log(JSON.stringify(reports));
