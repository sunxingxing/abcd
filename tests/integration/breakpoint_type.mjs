import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const context = vm.createContext({
  WidthBreakpoint: {
    WIDTH_SM: 'small', WIDTH_MD: 'medium', WIDTH_LG: 'large',
  },
});
const source = readFileSync(process.argv[2], 'utf8');
assert.match(source, /constructor\(sm, md, lg, xl\)/);
assert.match(source, /\["getValue"\]\(currentWidthBreakpoint\)/);
const recovered = new vm.SourceTextModule(source, {context});
await recovered.link(() => { throw new Error('unexpected import'); });
await recovered.evaluate();
const Type = recovered.namespace.site_7531_9();
assert.equal(Type.name, 'BreakpointType');
assert.equal(Type.length, 4);
const values = [Symbol('sm'), Symbol('md'), Symbol('lg'), Symbol('xl')];
const instance = new Type(...values);
assert.deepEqual(Object.keys(instance), ['sm', 'md', 'lg', 'xl']);
for (const [field, value] of Object.entries({
  sm: values[0], md: values[1], lg: values[2], xl: values[3],
})) {
  assert.deepEqual(Object.getOwnPropertyDescriptor(instance, field), {
    value, writable: true, enumerable: true, configurable: true,
  });
}
assert.equal(instance.getValue('small'), values[0]);
assert.equal(instance.getValue('medium'), values[1]);
assert.equal(instance.getValue('large'), values[2]);
assert.equal(instance.getValue('unknown'), values[3]);
const writes = [];
for (const field of ['sm', 'md', 'lg', 'xl']) {
  Object.defineProperty(Type.prototype, field, {
    set(value) { writes.push([field, value, this]); },
  });
}
const withSetters = new Type(...values);
assert.deepEqual(Object.keys(withSetters), []);
assert.deepEqual(writes.map(([field, value]) => [field, value]),
                 [['sm', values[0]], ['md', values[1]],
                  ['lg', values[2]], ['xl', values[3]]]);
assert(writes.every(([, , receiver]) => receiver === withSetters));

const lexicalContext = vm.createContext({
  WidthBreakpoint: {WIDTH_MD: 'object-value'},
});
vm.runInContext('let WidthBreakpoint = {WIDTH_MD: "lexical-value"};',
                lexicalContext);
const lexicalModule = new vm.SourceTextModule(source, {context: lexicalContext});
await lexicalModule.link(() => { throw new Error('unexpected import'); });
await lexicalModule.evaluate();
const LexicalType = lexicalModule.namespace.site_7531_9();
assert.equal(new LexicalType(...values).getValue('lexical-value'), values[1]);

const missingContext = vm.createContext({});
const missingModule = new vm.SourceTextModule(source, {context: missingContext});
await missingModule.link(() => { throw new Error('unexpected import'); });
await missingModule.evaluate();
const MissingType = missingModule.namespace.site_7531_9();
assert.throws(() => new MissingType(...values).getValue('medium'),
              error => error.name === 'ReferenceError');
