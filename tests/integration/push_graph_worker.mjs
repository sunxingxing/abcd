import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {isDeepStrictEqual, inspect} from 'node:util';
import {exercisePush} from './push_graph_state.mjs';

const input = JSON.parse(readFileSync(0, 'utf8'));
const records = [];
for (const config of input.cases) {
  const expected = await exercisePush(input.reference, config);
  const actual = await exercisePush(input.sources, config);
  if (input.mutation) {
    assert.equal(isDeepStrictEqual(actual, expected), false, input.mutation);
  } else if (!isDeepStrictEqual(actual, expected)) {
    const key = Object.keys(expected).find(key => !isDeepStrictEqual(actual[key], expected[key]));
    throw new Error('push graph difference ' + key + ' ' + JSON.stringify(config) + '\n' +
      inspect({actual: actual[key], expected: expected[key]}, {depth: 7, maxArrayLength: 30}));
  }
  if (!config.failAt && !input.mutation) {
    assert.equal(actual.failed, false);
    if (Object.keys(config).length === 0) {
      assert.equal(actual.diagnostics, 0);
      assert.ok(actual.platform.completions.every(([status]) => status === 'fulfilled'));
    }
    assert.ok(actual.modules.every(([, status]) => status === 'evaluated'));
    const required = ['WidgetCard', 'Index', 'CalleePage', 'CallComponent', 'TestAbilityPage', 'GetTokenPage', 'ExamplePage'];
    if (!config.windowCode) for (const name of required) assert.ok(actual.rendered.includes(name), 'unrendered real view ' + name);
  }
  const anchors = new Map();
  for (const [ordinal, name, key] of actual.sites)
    if ((name.startsWith('platform.') || name.startsWith('ability.') || name === 'ui.callback') && !anchors.has(name + ':' + key))
      anchors.set(name + ':' + key, ordinal);
  records.push({events: actual.events, views: [...new Set(actual.views.map(([name]) => name))],
    rendered: [...new Set(actual.rendered)], failed: actual.failed,
    ...(input.inventory ? {anchors: [...anchors.values()]} : {})});
}
console.log(JSON.stringify(records));
