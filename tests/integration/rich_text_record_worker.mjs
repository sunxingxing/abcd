import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {isDeepStrictEqual, inspect} from 'node:util';
import {exerciseRichText} from './rich_text_record_state.mjs';

const input = JSON.parse(readFileSync(0, 'utf8')), reports = [];
for (const config of input.cases) {
  const expected = await exerciseRichText(input.reference, config), actual = await exerciseRichText(input.sources, config);
  if (input.mutation) assert.equal(isDeepStrictEqual(actual, expected), false, input.mutation);
  else if (!isDeepStrictEqual(actual, expected)) {
    const key = Object.keys(expected).find(key => !isDeepStrictEqual(actual[key], expected[key]));
    throw new Error('rich text difference ' + key + ' ' + JSON.stringify(config) + '\n' +
      inspect({actual: actual[key], expected: expected[key]}, {depth: 8, maxArrayLength: 55, maxStringLength: 300}));
  }
  if (!config.failAt && !input.mutation) {
    assert.equal(actual.diagnostics, 0); assert.ok(actual.modules.every(([, status]) => status === 'evaluated'));
    const required = config.registration === false ? ['PasteBoard', 'ShareDirect'] : ['PasteBoard', 'ShareDirect', 'PageA', 'PageB'];
    for (const name of required) assert.ok(actual.rendered.includes(name), 'real view unrendered ' + name);
    if (Object.keys(config).length === 0) assert.ok(actual.platform.completions.every(([status]) => status === 'fulfilled'));
  }
  const anchors = new Map();
  for (const [ordinal, name, key] of actual.sites)
    if ((name.startsWith('platform.') || name.endsWith('.call') || name === 'ui.call') && !anchors.has(name + ':' + key))
      anchors.set(name + ':' + key, ordinal);
  reports.push({events: actual.events, failed: actual.failed, rendered: [...new Set(actual.rendered)],
    ...(input.inventory ? {anchors: [...anchors.values()], requests: actual.platform.requests,
      controllers: actual.platform.controllers, records: actual.platform.records, clicks: actual.clicks} : {})});
}
console.log(JSON.stringify(reports));
