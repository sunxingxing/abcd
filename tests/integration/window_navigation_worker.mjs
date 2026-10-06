import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {isDeepStrictEqual} from 'node:util';
import vm from 'node:vm';
import {exerciseNavigation} from './navigation_graph_state.mjs';

const {sources, reference, request} = JSON.parse(readFileSync(0, 'utf8'));
const failures = [undefined, null, 0, Symbol('window graph failure'), {}];
const config = {finalize: 'own', realWindow: true, ...request.config};
if (Object.hasOwn(config, 'failureIndex')) config.failure = failures[config.failureIndex];
const expected = await exerciseNavigation(reference, config);
let changed = sources;
if (request.mutation || request.equivalent) {
  const [name, before, after] = request.mutation ?? request.equivalent;
  const text = sources[name].replace(before, after); assert.notEqual(text, sources[name]);
  new vm.SourceTextModule(text); changed = {...sources, [name]: text};
}
let actual, rejected;
try {actual = await exerciseNavigation(changed, config);}
catch (error) {if (!request.mutation) throw error; rejected = error instanceof assert.AssertionError;}
if (request.mutation) assert.ok(rejected || !isDeepStrictEqual(actual, expected), 'undetected valid JS mutation');
else assert.ok(isDeepStrictEqual(actual, expected), 'whole window/navigation graph mismatch');
if (!request.mutation && config.failAt === undefined && !config.swapAt) {
  assert.ok(expected.modules.every(([, status]) => status === 'evaluated'));
  assert.equal(expected.modules.length, 33);
  assert.equal(expected.platform.instances.length, 2);
  for (const number of [0, 1]) for (const name of ['windowSizeChange', 'avoidAreaChange', 'windowTitleButtonRectChange', 'windowStatusChange'])
    assert.ok(expected.platform.listeners.some(entry => entry[0] === number && entry[1] === name));
  assert.equal(expected.platform.replayed, expected.platform.replayCandidates);
  assert.ok(expected.trace.every(([name]) => !name.endsWith('.throw')));
  assert.ok(expected.platform.requests > 0); assert.ok(expected.platform.completions.length > 0);
  if (config.initial !== false) for (const Type of ['Content', 'Header', 'Footer', 'Home', 'HomeContent'])
    assert.ok(expected.pages.includes(Type), 'missing actual view ' + Type);
  if (config.initial !== false) assert.ok(expected.cells.some(([, name, value]) => name === 'mainWindowInfo' && value === 'info1'));
}
if (config.failAt !== undefined) {
  assert.equal(expected.failed, true);
  const retained = expected.sites.find(([ordinal, name]) => ordinal === config.failAt && name === 'platform.listener');
  if (retained) assert.ok(expected.platform.replayedCallbacks.includes(retained[4]), 'exact retained window callback was not replayed');
}
const anchors = expected.sites.filter(([, name]) => name.startsWith('platform.'));
const seen = new Set();
const boundaries = anchors.filter(([, name, key]) => {
  const signature = JSON.stringify([name, typeof key === 'string' ? key : null]);
  if (seen.has(signature)) return false; seen.add(signature); return true;
}).map(([ordinal]) => ordinal);
process.stdout.write(JSON.stringify({events: expected.events, boundaries, requests: expected.platform.requests,
  modules: expected.modules.length, views: expected.views.length, instances: new Set(expected.instances).size,
  // The route root is private to its module, so it has no export-inventory
  // label. Its real body stays in the trace, but cannot inflate exported-type
  // construction/render coverage through JSON's undefined-to-null conversion.
  viewNames: expected.views, instanceNames: expected.instances.filter(name => name !== undefined),
  renderedNames: expected.renderedViews.filter(name => name !== undefined),
  mutation: !!request.mutation, equivalent: !!request.equivalent, failed: expected.failed}));
