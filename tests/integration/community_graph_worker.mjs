import assert from 'node:assert/strict';
import {AssertionError} from 'node:assert';
import {readFileSync} from 'node:fs';
import {isDeepStrictEqual, types} from 'node:util';
import {exerciseCommunity, verifyCommunityBatch} from './community_graph_state.mjs';

const {sources, reference, cases, inventory, mutation} = JSON.parse(readFileSync(0, 'utf8'));
if (mutation) {
  const expected = await exerciseCommunity(reference, cases[0]);
  assert.equal(expected.diagnostics.length, 0);
  assert.ok(expected.trace.every(([name]) => !name.endsWith('.throw')));
  global.gc?.();
  let rejected = false;
  try {
    const actual = await exerciseCommunity(sources, cases[0]);
    delete expected.diagnostics; delete actual.diagnostics;
    rejected = !isDeepStrictEqual(actual, expected);
  } catch (error) {
    // Strict host assertions and native export-link failures are contracts
    // too. Baseline runs propagate them; only a parsed, intentional mutant may
    // be rejected this way, after an independently successful reference run.
    if (!(error instanceof AssertionError || types.isNativeError(error) && error.name === 'SyntaxError')) throw error;
    rejected = true;
  }
  assert.equal(rejected, true, 'undetected valid-JS mutation ' + mutation);
  process.stdout.write(JSON.stringify({mutations: 1}));
} else {
  process.stdout.write(JSON.stringify(await verifyCommunityBatch(sources, reference, cases, inventory)));
}
