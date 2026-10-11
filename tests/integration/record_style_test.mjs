import assert from 'node:assert/strict';
import vm from 'node:vm';
import {classMethodProfiles} from './record_style.mjs';

const reference = `class Example { choose(value, host) {
  switch (value) { case 1: host.first(); break; case 2: host.second(); break; default: host.other(); }
} }`;
const conditional = `class Renamed { choose(input, receiver) {
  if (input === 1) receiver.first(); else if (input === 2) receiver.second(); else receiver.other();
} }`;
assert.deepEqual(classMethodProfiles(reference), classMethodProfiles(conditional));
for (const value of [undefined, null, false, 0, 1, 2, 3, '1', {}, Symbol('value')]) {
  const calls = source => {
    const Type = vm.runInNewContext(source + '; ' + (source === reference ? 'Example' : 'Renamed'));
    const trace = [], host = Object.fromEntries(['first', 'second', 'other'].map(name => [name, function() {
      assert.equal(this, host); trace.push(name);
    }]));
    new Type().choose(value, host); return trace;
  };
  assert.deepEqual(calls(reference), calls(conditional));
}
const dispatcher = reference.replace('switch (value)', 'while (true) { switch (value)').replace(/\n\} \}$/, '\n break; } } }');
assert.notDeepEqual(classMethodProfiles(dispatcher), classMethodProfiles(reference));
assert.notDeepEqual(classMethodProfiles(reference.replace('host.first()', 'host.wrong()')), classMethodProfiles(reference));
const loop = 'class Example { run(host) { for (let i=0; i<2; ++i) host.next(); } }';
assert.notDeepEqual(classMethodProfiles(loop), classMethodProfiles(loop.replace('for (let i=0; i<2; ++i)', 'if (true)')));
console.log('native branch alternatives pass; dispatcher, lost loop and wrong call rejected');
