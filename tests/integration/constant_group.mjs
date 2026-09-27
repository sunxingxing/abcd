import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const source = execFileSync(process.argv[2], {encoding: 'utf8'});
const {cases} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
const expected = [NaN, Infinity, undefined, null, true, false];
const profileCount = 3;
const forms = 2;
assert.equal(cases.length, profileCount * expected.length * forms);
for (let profile = 0; profile < profileCount; ++profile) {
  for (let value = 0; value < expected.length; ++value) {
    const index = (profile * expected.length + value) * forms;
    for (let form = 0; form < forms; ++form) {
      const original = cases[index + form];
      // These names are mutable lexical bindings, not the VM constant table.
      const shadowed = Function('NaN', 'Infinity', 'undefined',
        `"use strict"; return (${original.toString()});`)(17, -3, 'shadow');
      for (const recovered of [original, shadowed]) {
        assert.equal(recovered.length, form);
        if (form === 0) {
          assert.ok(Object.is(recovered(), expected[value]));
          continue;
        }
        const events = [];
        const actual = recovered(function(argument) {
          assert.equal(this, undefined);
          events.push(argument);
          return {different: true};
        });
        assert.ok(Object.is(actual, expected[value]));
        assert.equal(events.length, 1);
        assert.ok(Object.is(events[0], expected[value]));
        const marker = {};
        assert.throws(() => recovered(argument => {
          assert.ok(Object.is(argument, expected[value]));
          throw marker;
        }), error => error === marker);
        assert.throws(() => recovered(null), TypeError);
      }
    }
  }
}
