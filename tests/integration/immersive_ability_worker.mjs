import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {isDeepStrictEqual} from 'node:util';
import {exerciseAbility} from './immersive_ability_state.mjs';

const {sources, reference, cases, mutation = false} = JSON.parse(readFileSync(0, 'utf8'));
const results = [];
for (const config of cases) {
  const expected = await exerciseAbility(reference, config);
  let actual, rejected = false;
  try {actual = await exerciseAbility(sources, config, true);}
  catch (error) {if (!mutation) throw error; rejected = error instanceof assert.AssertionError;}
  if (mutation) assert.ok(rejected || !isDeepStrictEqual(actual, expected), 'undetected valid JS mutation');
  else assert.deepEqual(actual, expected, JSON.stringify(config));
  if (config.failAt) {
    assert.equal(expected.failed, true, 'fault site reached');
    const site = expected.sites.find(([id]) => id === config.failAt);
    if (site[1] === 'ability.retained') assert.ok(expected.replayed.includes(site[2]), 'retained callback survived registration failure');
  }
  if (!config.failAt && !config.swapAt) {
    assert.equal(expected.modules.length, 5); assert.ok(expected.modules.every(([, status]) => status === 'evaluated'));
    // The actual catch reads error.code: null/undefined setter throws abort
    // before loadContent, while other primitive/object failures can be logged.
    const callbacks = config.windowSetter === 'throw' && config.failureIndex < 2 ? 0 : 4;
    assert.equal(expected.callbacks, callbacks); assert.equal(expected.replayed.length, callbacks * 2);
    assert.equal(expected.utilities.length, 4);
    if (!config.code && config.windowSetter !== 'throw')
      assert.ok(expected.platform.listeners.length > 0, 'actual WindowUtil registered callbacks');
  }
  results.push({events: expected.events, sites: expected.sites.map(([id, name, key]) => [id, name, key]),
    callbacks: expected.callbacks, replays: expected.replayed.length, listeners: expected.platform.listeners.length});
}
process.stdout.write(JSON.stringify(results));
