import assert from 'node:assert/strict';
import vm from 'node:vm';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {editExpression} from './expression_edits.mjs';

// Execute the entire module and its real class. Only platform services are
// controlled here; constructors/methods/accessors/callbacks are never replaced.
async function exercise(source, config = {}) {
  const trace = [], callbacks = [], clicks = [], owners = [], cells = [];
  const tags = new WeakMap(), failure = Object.hasOwn(config, 'failure') ? config.failure : {};
  const token = {get then() {assert.fail('void host result was assimilated');}};
  tags.set(token, 'void result');
  let context, Type, nextElement = 0, failed = false;
  const tag = (object, name) => {tags.set(object, name); return object;};
  const normalize = input => {
    if (input === null || typeof input !== 'object' && typeof input !== 'function') return input;
    if (tags.has(input)) return tags.get(input);
    if (typeof input === 'function') return ['callback', input.name, input.length, Object.hasOwn(input, 'prototype')];
    return Reflect.ownKeys(input).map(key => {
      const d = Object.getOwnPropertyDescriptor(input, key);
      assert.ok('value' in d, `unobserved accessor ${String(key)}`);
      return [key, normalize(d.value), d.writable, d.enumerable, d.configurable];
    });
  };
  let components, subscriber;
  const event = (name, ...args) => {
    trace.push([name, ...args.map(normalize)]);
    if (trace.length === config.swapAt) {
      for (const [name, object] of components[1]) context[name] = object;
      context.SubscriberManager = subscriber[1]; trace.push(['globals.swap']);
    }
    if (trace.length === config.failAt) {failed = true; throw failure;}
  };
  const outcome = error => {
    if (Object.is(error, failure)) return 'original throw';
    assert.ok(['TypeError', 'ReferenceError'].includes(error?.name), String(error));
    return error.name;
  };
  const call = (name, fn, receiver, args = []) => {
    try {assert.equal(Reflect.apply(fn, receiver, args), undefined); trace.push([`${name}.return`]);}
    catch (error) {trace.push([`${name}.throw`, outcome(error)]);}
  };
  function surface(name, methods) {
    let object;
    object = new Proxy({}, {get(_target, key, receiver) {
      assert.equal(receiver, object); assert.ok(Object.hasOwn(methods, key), `${name}.${String(key)}`);
      event(`${name}.get`, key);
      const method = methods[key];
      if (typeof method !== 'function') return method;
      const fn = function(...args) {
        assert.equal(this, object); event(`${name}.call`, key, ...args); return method(...args);
      };
      for (const key of ['call', 'apply', 'bind']) Object.defineProperty(fn, key, {
        get() {assert.fail('ambient invocation adapter');}});
      return fn;
    }});
    return tag(object, name);
  }
  const cell = (owner, key, initial, alias) => {
    const entry = {owner, key, alias, value: initial, deleted: false, purged: [], reads: 0};
    const object = surface(`cell${cells.length}:${alias}`, {
      get() {
        ++entry.reads;
        if (alias === 'image' && config.changingImage && entry.reads % 2 === 0) return 'second read';
        return entry.value;
      },
      set(value) {entry.value = value; return token;},
      purgeDependencyOnElmtId(id) {entry.purged.push(id); return token;},
      aboutToBeDeleted() {entry.deleted = true; return token;},
    });
    cells.push(entry); return object;
  };
  function ViewPU(...args) {
    Type ??= new.target;
    const owner = config.baseObject ? Object.create(new.target.prototype) : this;
    tag(owner, `owner${owners.length}`); owners.push(owner); event('super', ...args);
    return config.baseObject ? owner : undefined;
  }
  const hostMethods = {
    createLocalStorageProp(key, initial, alias) {return cell(this, key, initial, alias);},
    observeComponentCreation2(callback, component) {
      assert.equal(callback.length, 2); assert.equal(Object.hasOwn(callback, 'prototype'), false);
      // Retain before the registration fault. The actual closure must remain
      // callable after its owner's method has failed or the owner is deleted.
      callbacks.push({owner: this, callback, component, id: ++nextElement});
      event('observe.retained', this, component);
      if (config.immediate) assert.equal(Reflect.apply(callback, {foreign: true}, [nextElement, true]), undefined);
      return token;
    },
    ifElseBranchUpdateFunction(branch, callback) {
      event('branch', this, branch);
      assert.equal(Reflect.apply(callback, {foreign: true}, []), undefined); return token;
    },
    id__() {return owners.indexOf(this) + 10;},
    aboutToBeDeletedInternal() {return token;},
    updateDirtyElements() {return token;},
  };
  for (const [name, body] of Object.entries(hostMethods)) Object.defineProperty(ViewPU.prototype, name, {
    configurable: true, get() {
      const owner = this; assert.ok(owners.includes(owner)); event('host.get', name, owner);
      return function(...args) {
        assert.equal(this, owner); event('host.call', name, owner, ...args); return Reflect.apply(body, owner, args);
      };
    }});
  const finalize = function() {assert.ok(owners.includes(this)); event('finalize', this); return token;};
  if (config.finalize === 'own') ViewPU.prototype.finalizeConstruction = finalize;
  if (config.finalize === 'inherited') Object.setPrototypeOf(ViewPU.prototype, {finalizeConstruction: finalize});
  if (config.finalize === 'noncallable') ViewPU.prototype.finalizeConstruction = 7;
  if (config.finalize === 'frozen') Object.preventExtensions(ViewPU.prototype);
  const ReflectHost = surface('Reflect', {set: (...args) => Reflect.set(...args)});
  tag(ViewPU.prototype, 'ViewPU.prototype');
  components = [0, 1].map(version => new Map(['Flex', 'Row', 'Text', 'Span', 'If', 'Image'].map(name =>
    [name, surface(`${name}${version}`, Object.fromEntries(['create', 'pop', 'padding', 'fontSize', 'height', 'onClick']
      .map(key => [key, (...args) => {
        if (key === 'onClick') {assert.equal(args[0].length, 0); clicks.push(args[0]);}
        return token;
      }])))])));
  subscriber = [0, 1].map(version => {
    const instance = surface(`subscriber${version}`, {delete: () => token});
    return surface(`SubscriberManager${version}`, {Get: () => instance});
  });
  let storage, launched;
  context = vm.createContext({ViewPU, Reflect: ReflectHost, SubscriberManager: subscriber[0],
    ...Object.fromEntries(components[0]), FlexDirection: surface('direction', {Column: 'column'}),
    LocalStorage: function() {storage = tag(this, 'storage'); event('storage.new');},
    ViewStackProcessor: surface('stack', {
      AllocateNewElmetIdForNextComponent: () => 17,
      StartGetAccessRecordingFor: id => {assert.equal(id, 17); return token;},
      StopGetAccessRecording: () => token,
    }),
    loadEtsCard(owner, path) {
      assert.equal(this, undefined); launched = owner; event('launch', owner, path); return token;
    },
    postCardAction(owner, value) {
      assert.equal(this, undefined); assert.ok(owners.includes(owner)); event('action', owner, value); return token;
    },
  });
  const module = new vm.SourceTextModule(source, {context});
  await module.link(() => assert.fail('WidgetCard has no imports'));
  assert.deepEqual(Object.keys(module.namespace), []);
  let moduleError, moduleFailed = false;
  try {await module.evaluate(); trace.push(['module.return']);}
  catch (error) {moduleError = error; moduleFailed = true; trace.push(['module.throw', outcome(error)]);}
  const beforeRepeat = trace.length;
  if (moduleFailed) await assert.rejects(module.evaluate(), error => Object.is(error, moduleError));
  else await module.evaluate();
  assert.equal(trace.length, beforeRepeat, 'evaluation repeated module side effects');
  let assignment, construction;
  if (!moduleFailed) {
    assert.equal(launched, owners[0]); assert.ok(storage);
    assert.equal(Type.name, 'WidgetCard'); assert.equal(Type.length, 3);
    assert.equal(Type.getEntryName(), 'WidgetCard');
    const entry = Object.getOwnPropertyDescriptor(Type, 'getEntryName');
    assert.equal(entry.value.name, 'getEntryName'); assert.equal(entry.value.length, 0);
    assert.equal(entry.writable, true); assert.equal(entry.enumerable, false); assert.equal(entry.configurable, true);
    assert.equal(Object.getPrototypeOf(Type), ViewPU);
    assert.equal(Object.getPrototypeOf(Type.prototype), ViewPU.prototype);
    assert.throws(() => Type(), error => error.name === 'TypeError');
    for (const [name, length] of [['setInitiallyProvidedValue', 1], ['updateStateVars', 1],
      ['purgeVariableDependenciesOnElmtId', 1], ['aboutToBeDeleted', 0], ['initialRender', 0], ['rerender', 0]]) {
      const d = Object.getOwnPropertyDescriptor(Type.prototype, name);
      assert.equal(d.value.name, name); assert.equal(d.enumerable, false);
      assert.equal(d.writable, true); assert.equal(d.configurable, true);
      assert.equal(d.value.length, length); assert.equal(Object.hasOwn(d.value, 'prototype'), false);
    }
    for (const name of ['formId', 'text', 'image']) {
      const d = Object.getOwnPropertyDescriptor(Type.prototype, name);
      assert.equal(d.get.name, `get ${name}`); assert.equal(d.get.length, 0);
      assert.equal(d.set.name, `set ${name}`); assert.equal(d.set.length, 1);
      assert.equal(d.enumerable, false); assert.equal(d.configurable, true);
    }
    const poisonParams = new Proxy({}, {get() {assert.fail('empty initialization methods read params');}});
    const lambda = tag(() => poisonParams, 'lambda');
    const assignmentOwner = Type.prototype;
    let assignedReceiver, assignedValue, writes = 0;
    if (config.assignment) Object.defineProperty(assignmentOwner, config.assignment, {
      configurable: true, set(value) {
        ++writes; assignedReceiver = this; assignedValue = value;
        event('field.assignment', this, config.assignment, value);
        if (config.setterThrows) throw failure;
        Object.defineProperty(this, config.assignment, {value, writable: true, enumerable: true, configurable: true});
      }});
    const alternate = Object.create(Type.prototype);
    const Target = function() {};
    Target.prototype = alternate;
    const supplied = ['parent', poisonParams, storage, config.elmtId, lambda, 'extra'];
    const args = Array.from({length: config.argumentCount ?? supplied.length}, (_, index) =>
      index < supplied.length ? supplied[index] : tag({}, `tail${index}`));
    try {
      const second = Reflect.construct(Type, args, config.alternate ? Target : Type);
      assert.equal(second.paramsGenerator_, args.length > 4 ? lambda : undefined);
      for (const name of ['__formId', '__text', '__image']) assert.notEqual(second[name], launched[name]);
    } catch (error) {trace.push(['second.throw', outcome(error)]);}
    construction = {keys: owners.map(owner => Reflect.ownKeys(owner)),
      fields: cells.map(entry => [tags.get(entry.owner), entry.alias])};
    if (config.assignment) {
      assignment = {writes, receiver: assignedReceiver && tags.get(assignedReceiver),
        value: assignedValue && tags.get(assignedValue),
        prototype: assignedReceiver && Object.getPrototypeOf(assignedReceiver) === (config.alternate ? alternate : Type.prototype)};
      delete assignmentOwner[config.assignment];
    }
    for (const owner of owners) {
      call('initialValues', owner.setInitiallyProvidedValue, owner, [poisonParams]);
      call('update', owner.updateStateVars, owner, [poisonParams]);
      for (const [name, value] of [['formId', 'form'], ['text', 'text'],
        ['image', Object.hasOwn(config, 'image') ? config.image : 'payload']]) {
        try {owner[name] = value; trace.push(['accessor.value', owner && tags.get(owner), name, normalize(owner[name])]);}
        catch (error) {trace.push(['accessor.throw', name, outcome(error)]);}
      }
      call('render', owner.initialRender, owner);
      call('rerender', owner.rerender, owner);
      call('purge', owner.purgeVariableDependenciesOnElmtId, owner, [config.elmtId]);
      call('delete', owner.aboutToBeDeleted, owner);
    }
    while (callbacks.length) {
      const batch = callbacks.splice(0);
      if (config.reverse) batch.reverse();
      for (const item of batch) for (const initial of [true, false])
        call('retained.callback', item.callback, {foreign: true}, [item.id, initial]);
    }
    for (const click of clicks) call('click', click, {foreign: true});
  }
  const state = owners.map(owner => Reflect.ownKeys(owner).map(key => {
    const d = Object.getOwnPropertyDescriptor(owner, key);
    assert.ok('value' in d); return [key, normalize(d.value), d.writable, d.enumerable, d.configurable];
  }));
  const cellState = cells.map(entry => [tags.get(entry.owner), entry.key, entry.alias,
    normalize(entry.value), entry.deleted, entry.purged]);
  return {trace, state, cellState, failed, moduleFailed, assignment, construction};
}

export async function verifyWidgetRecordBatch(source, reference, request) {
  for (const text of [source, reference]) new vm.SourceTextModule(text);
  for (const text of [source]) {
    assert.match(text, /class WidgetCard extends/);
    assert.match(text, /constructor\(parent, params, __localStorage, elmtId = /);
    assert.match(text, /super\(parent, __localStorage, elmtId, extraInfo\)/);
    assert.match(text, /observeComponentCreation2\(/); assert.match(text, /ifElseBranchUpdateFunction\(/);
    assert.match(text, /get formId\(\)/); assert.match(text, /set image\(newValue\)/);
    assert.doesNotMatch(text, /INCOMPLETE|UNRECOVERED|\$acc\b|\$r\d+\b|\bwhile\s*\(|Reflect\.construct/);
  }
  let cases = 0, faultCases = 0, swapCases = 0;
  const available = {lifecycle: 0, faults: 0, assignments: 0, mutations: 0};
  const selected = phase => {
    const ordinal = available[phase]++;
    return request.phase === phase && ordinal >= request.first && ordinal < request.first + request.count;
  };
  const compare = async config => {
    const actual = await exercise(source, config), expected = await exercise(reference, config);
    assert.deepEqual(actual, expected);
    if (config.failAt) assert.equal(actual.failed, true, `fault not reached: ${config.failAt}`);
    if (config.swapAt) assert.ok(actual.trace.some(item => item[0] === 'globals.swap'), `swap not reached: ${config.swapAt}`);
    ++cases; return actual;
  };
  for (const finalize of [undefined, 'own', 'inherited', 'noncallable', 'frozen'])
    for (const baseObject of [false, true]) for (const reverse of [false, true])
      for (const immediate of [false, true]) for (const changingImage of [false, true])
        for (const image of [undefined, null, false, 0, NaN, '', 'payload', Symbol('image'), 2n])
          if (selected('lifecycle')) await compare({finalize, baseObject, reverse, immediate, image, changingImage, elmtId: -0});
  for (const argumentCount of [0, 1, 2, 3, 4, 6, 200, 201]) for (const baseObject of [false, true])
    if (selected('lifecycle')) await compare({finalize: 'own', argumentCount, baseObject, image: 'payload'});
  const baseline = await exercise(source, {finalize: 'own', image: 'payload'});
  assert.deepEqual(baseline, await exercise(reference, {finalize: 'own', image: 'payload'}));
  assert.equal(baseline.moduleFailed, false);
  assert.ok(baseline.trace.some(item => item[0] === 'action'));
  assert.ok(baseline.trace.some(item => item[0] === 'retained.callback.return'));
  for (let failAt = 1; failAt <= baseline.trace.length; ++failAt) {
    // Only original observable events are injected; outcome markers are not
    // host operations and must not inflate the claimed fault count.
    if (/\.(return|throw)$/.test(baseline.trace[failAt - 1][0]) || baseline.trace[failAt - 1][0] === 'accessor.value') continue;
    for (const failure of [undefined, null, 0, Symbol('failure'), {}]) {
      if (selected('faults')) {
        const actual = await compare({finalize: 'own', image: 'payload', failAt, failure});
        if (baseline.trace[failAt - 1][0] === 'observe.retained') {
          const thrown = actual.trace.findIndex(item => item[0].endsWith('.throw') && item[1] === 'original throw');
          assert.ok(thrown >= 0);
          assert.ok(actual.trace.slice(thrown + 1).some(item => item[0].startsWith('retained.callback.')),
            'registration fault discarded retained callbacks');
        }
        ++faultCases;
      }
    }
    if (selected('faults')) {await compare({finalize: 'own', image: 'payload', swapAt: failAt}); ++swapCases;}
  }
  // Pinned STOBJBYNAME is assignment. TS native DefineField is deliberately
  // not this layer's oracle: prove actual inherited setter dispatch, partial
  // construction and alternate new.target against the expected write event.
  let assignmentCases = 0;
  const fieldNames = ['__formId', '__text', '__image'];
  for (const assignment of fieldNames)
    for (const setterThrows of [false, true]) for (const alternate of [false, true])
      for (const failure of setterThrows ? [undefined, null, 0, Symbol('setter'), {}] : [{}]) {
      if (!selected('assignments')) continue;
      const actual = await exercise(source, {finalize: 'own', assignment, setterThrows, alternate, failure});
      assert.equal(actual.assignment.writes, 1);
      assert.equal(actual.assignment.receiver, 'owner1'); assert.equal(actual.assignment.prototype, true);
      assert.match(actual.assignment.value, new RegExp(`:${assignment.slice(2)}$`));
      assert.equal(actual.trace.some(item => item[0] === 'second.throw' && item[1] === 'original throw'), setterThrows);
      assert.deepEqual(actual.construction.keys[1], setterThrows ? fieldNames.slice(0, fieldNames.indexOf(assignment)) :
        [...fieldNames, 'paramsGenerator_']);
      assert.deepEqual(actual.construction.fields.filter(([owner]) => owner === 'owner1').map(([, name]) => name),
        fieldNames.slice(0, setterThrows ? fieldNames.indexOf(assignment) + 1 : fieldNames.length).map(name => name.slice(2)));
      ++assignmentCases;
    }
  let mutations = 0;
  for (const [from, to] of [
    ['this.__text.set(newValue)', 'this.__text.set(void 0)'],
    ['this.__image.purgeDependencyOnElmtId(rmElmtId)', 'this.__text.purgeDependencyOnElmtId(rmElmtId)'],
    ['this.__text.aboutToBeDeleted();', ''],
    ['SubscriberManager.Get()["delete"](this.id__())', 'SubscriberManager.Get()["delete"]((this.id__(), this.id__()))'],
    ['this.updateDirtyElements();', 'return this.updateDirtyElements();'],
    ['StopGetAccessRecording();', 'StartGetAccessRecordingFor(17);'],
    ['abilityName: "EntryAbility"', 'abilityName: "WrongAbility"'],
    ['elmtId = (-1)', 'elmtId = 0'],
    ['paramsLambda = (void 0)', 'paramsLambda = (() => ({}))'],
    ['super(parent, __localStorage, elmtId, extraInfo)', 'super(parent, __localStorage, elmtId, params)'],
  ]) {
    if (!selected('mutations')) continue;
    assert.ok(source.includes(from), from); const changed = source.replace(from, to);
    new vm.SourceTextModule(changed);
    await assert.rejects(async () => assert.deepEqual(await exercise(changed, {finalize: 'own', image: 'payload'}), baseline),
      error => error instanceof assert.AssertionError, from); ++mutations;
  }
  if (selected('mutations')) {
    // The first saved receiver must retain its identity whether represented by
    // a declaration or an assignment. Private register names are not the oracle.
    const changed = editExpression(source, node =>
      node.type === 'VariableDeclarator' && node.init?.type === 'ThisExpression' ||
      node.type === 'AssignmentExpression' && node.operator === '=' && node.right.type === 'ThisExpression',
    (node, text) => node.type === 'VariableDeclarator' ? `${text(node.id)} = globalThis` :
      `${text(node.left)} = globalThis`, {first: true});
    new vm.SourceTextModule(changed);
    await assert.rejects(async () => assert.deepEqual(await exercise(changed, {finalize: 'own', image: 'payload'}), baseline),
      error => error instanceof assert.AssertionError, 'saved receiver identity');
    ++mutations;
  }
  if (selected('mutations')) {
    const field = '(this.__formId = this.createLocalStorageProp("formId", "", "formId"));';
    assert.ok(source.includes(field));
    const changed = source.replace(field, 'Object.defineProperty(this, "__formId", {value: this.createLocalStorageProp("formId", "", "formId"), writable: true, enumerable: true, configurable: true});');
    new vm.SourceTextModule(changed);
    const mutant = await exercise(changed, {finalize: 'own', assignment: '__formId'});
    assert.throws(() => assert.equal(mutant.assignment.writes, 1), error => error instanceof assert.AssertionError);
    ++mutations;
  }
  return {available, cases, faultCases, swapCases, assignmentCases, mutations};
}

export async function verifyWidgetRecord(source, reference) {
  // Bound independent VM retention with sequential, finite test batches. Each
  // child executes full ESM modules; no source truncation or engine scheduling
  // change is involved. This batch width is a test resource choice, not proof
  // that the production decompiler's process RSS meets its separate limit.
  const worker = fileURLToPath(new URL('./widget_record_worker.mjs', import.meta.url));
  const run = request => JSON.parse(execFileSync(process.execPath, ['--expose-internals', '--experimental-vm-modules', worker], {
    input: JSON.stringify({source, reference, ...request}), encoding: 'utf8', timeout: 60_000,
    stdio: ['pipe', 'pipe', 'pipe'], maxBuffer: 1024 * 1024,
  }));
  const inventory = run({phase: 'inventory', first: 0, count: 0}).available;
  const result = {cases: 0, faultCases: 0, swapCases: 0, assignmentCases: 0, mutations: 0};
  const batchWidth = 128;
  for (const [phase, count] of Object.entries(inventory)) for (let first = 0; first < count; first += batchWidth) {
    const row = run({phase, first, count: Math.min(batchWidth, count - first)});
    assert.deepEqual(row.available, inventory);
    for (const key of Object.keys(result)) result[key] += row[key];
  }
  assert.equal(result.cases, inventory.lifecycle + inventory.faults);
  assert.equal(result.faultCases + result.swapCases, inventory.faults);
  assert.equal(result.assignmentCases, inventory.assignments);
  assert.equal(result.mutations, inventory.mutations);
  return {...result, wholeRecordBehavior: true, platformExecution: false};
}
