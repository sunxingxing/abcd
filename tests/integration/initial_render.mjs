import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {dirname, join} from 'node:path';
import vm from 'node:vm';

const generated = execFileSync(process.argv[2], [process.argv[3], '--render'], {encoding: 'utf8'});
// The paired source is read only by this independent oracle. The engine above
// receives the ABC path and the fixture selector, never a source body or answer.
const paired = readFileSync(join(dirname(process.argv[3]),
  'Immersive_src/default/src/main/ets/pages/Index.ts'), 'utf8');
const begin = paired.indexOf('    initialRender() {');
const end = paired.indexOf('    rerender()', begin);
assert.ok(begin >= 0 && end > begin);
const reference = paired.slice(begin, end).replaceAll('routerItem: Route', 'routerItem').replaceAll('route: Route', 'route');
let scenarios = 0;
function run(source, variant, initial, deferred, mutate, failure) {
  const events = [], pending = [], clicks = [], builders = [], tags = new WeakMap();
  const exception = {};
  let receiver, generation = 0, element = 0;
  const item = index => ({title: `item${index}`, name: `name${index}`, to: `page${index}`});
  const choices = [undefined, null, false, 0, '', [], [item(0)], [item(0), item(1)], {truthy: true}];
  const payload = choices[variant];
  if (payload && typeof payload === 'object') tags.set(payload, 'children');
  const normalize = value => {
    if (typeof value === 'function') return 'callback';
    if (!value || typeof value !== 'object') return value;
    if (tags.has(value)) return tags.get(value);
    if (Array.isArray(value)) return Array.from(value, normalize);
    return Object.fromEntries(Object.entries(value).map(([key, member]) => [key, normalize(member)]));
  };
  const event = (name, ...args) => {
    events.push([name, ...args.map(normalize)]);
    if (failure === name) throw exception;
  };
  const route = {title: 'section', get children() {
    assert.equal(this, route); event('children:get');
    if (mutate) ++generation;
    return payload;
  }};
  tags.set(route, 'route');
  const globals = {ROUTES: [route], NavigationMode: {Stack: 'stack'}, FontWeight: {Bold: 'bold', Medium: 'medium'}, Color: {White: 'white'}};
  for (const name of ['Navigation', 'Column', 'Text', 'ForEach', 'If', 'Divider', 'Row', 'Span', 'Image']) {
    const component = new Proxy({}, {get(target, operation) {
      event(`${name}.${String(operation)}:get`);
      return function(...args) {
        assert.equal(this, component); event(`${name}.${String(operation)}:call`, ...args);
        if (operation === 'onClick') clicks.push(args[0]);
        if (operation === 'navDestination') builders.push(args[0].builder);
      };
    }});
    tags.set(component, name); globals[name] = component;
  }
  globals.JSON = {stringify(value) {
    assert.equal(this, globals.JSON); event('stringify', value);
    return JSON.stringify(value);
  }};
  receiver = {
    navPageInfos: {},
    NavPageMap(name) { assert.equal(this, receiver); event('destination', name); },
    go(name) { assert.equal(this, receiver); event('go', name); return name; },
    get observeComponentCreation2() {
      event('observe:get');
      return function(callback, kind) {
        assert.equal(this, receiver); event('observe:call', kind);
        const id = ++element;
        const invoke = () => callback.call({wrongReceiver: true}, id, initial);
        if (deferred) pending.push(invoke); else invoke();
      };
    },
    ifElseBranchUpdateFunction(index, callback) {
      assert.equal(this, receiver); event('branch', index); callback.call({wrongReceiver: true});
    },
    get forEachUpdateFunction() {
      const selected = generation; event('foreach:get', selected);
      const method = function(id, items, generator, key, withIndex, keyWithIndex) {
        assert.equal(this, receiver); assert.equal(keyWithIndex, false);
        assert.equal(generator.name, 'forEachItemGenFunction');
        assert.equal(generator.length, withIndex ? 2 : 1);
        event('foreach:call', selected, id, items, withIndex);
        if (!Array.isArray(items)) { assert.equal(items, payload); return; }
        for (let index = 0; index < items.length; ++index) {
          event('key', key.call({wrongReceiver: true}, items[index]));
          if (withIndex) generator.call({wrongReceiver: true}, items[index], index);
          else generator.call({wrongReceiver: true}, items[index]);
        }
      };
      for (const property of ['call', 'apply', 'bind']) Object.defineProperty(method, property, {
        get() { throw new Error(`unexpected foreach ${property}`); },
      });
      return method;
    },
  };
  tags.set(receiver.navPageInfos, 'stack');
  const method = vm.runInNewContext(`(class { ${source} }).prototype.initialRender`, globals);
  assert.equal(method.length, 0);
  try {
    assert.equal(method.call(receiver), undefined);
    while (pending.length) pending.shift()();
    if (mutate && Array.isArray(payload)) for (const child of payload) child.to += '-updated';
    for (const click of clicks) event('click-result', click.call({wrongReceiver: true}));
    for (const builder of builders) assert.equal(builder.call({wrongReceiver: true}, 'Shopping'), undefined);
    events.push(['return']);
  } catch (error) {
    assert.equal(error, exception, 'unexpected runtime or oracle failure');
    events.push(['throw', 'same exception']);
  }
  return events;
}
for (let variant = 0; variant < 9; ++variant) for (const initial of [false, true])
  for (const deferred of [false, true]) for (const mutate of [false, true])
    for (const failure of ['', 'observe:call', 'children:get', 'foreach:get', 'foreach:call',
      'stringify', 'Row.onClick:call', 'Divider.create:call', 'go', 'destination']) {
      assert.deepEqual(run(generated, variant, initial, deferred, mutate, failure),
        run(reference, variant, initial, deferred, mutate, failure));
      ++scenarios;
    }
console.log(`initialRender: ${scenarios} paired-source framework scenarios passed`);
