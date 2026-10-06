import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
import {parseJavaScript} from './class_reference.mjs';

// Execute only the registered copy-button callback. Construction, storage,
// paste progress, real files, clipboard and platform rendering are not exercised.
const [tool, root] = process.argv.slice(2);
const temporary = mkdtempSync(join(tmpdir(), 'abcd-progressbar-cell-'));
const text = 'a'.repeat(2 * 1024 * 1024);
async function trace(source, mode, replace, failAt) {
  const events = [], registrations = [], handlers = [], labels = new Map();
  const failure = {code: 7, message: 'failure'};
  let active = false, steps = 0;
  const event = (name, ...args) => {
    if (!active) return;
    events.push([name, ...args]);
    if (++steps === failAt) throw failure;
  };
  const host = (label, methods) => {
    const object = {};
    for (const [name, implementation] of Object.entries(methods)) Object.defineProperty(object, name, {
      configurable: true, get() {
        event(`${label}.get:${name}`);
        return function(...args) {
          assert.equal(this, object); event(`${label}.call:${name}`);
          return implementation(...args);
        };
      }
    });
    return object;
  };
  const ui = host('ui', {getHostContext() {
    return {get distributedFilesDir() { event('distributed'); return '/shared'; },
      get filesDir() { event('files'); return '/local'; }};
  }});
  const file = {get fd() { event('fd'); return 23; }};
  const pasteData = {};
  const board = host('board', {setData(value) {
    assert.equal(value, pasteData);
    const promise = mode === 'reject' ? Promise.reject(failure) : mode === 'thenable' ?
      Promise.resolve({then(resolve) { event('thenable'); resolve(); }}) : Promise.resolve();
    return promise;
  }});
  const io = host('io', {
    openSync(path, flags) { assert.equal(path, '/local/test1.txt'); assert.equal(flags, 3); return file; },
    writeSync(fd, value) { assert.equal(fd, 23); assert.equal(value, text); return value.length; },
    closeSync(value) { assert.equal(value, file); }
  });
  Object.defineProperty(io, 'OpenMode', {get() {
    event('OpenMode');
    if (replace) Object.defineProperty(io, 'openSync', {value() { assert.fail('callee reread'); }});
    return {get READ_WRITE() { event('READ_WRITE'); return 1; }, get CREATE() { event('CREATE'); return 2; }};
  }});
  const uri = host('uri', {getUriFromPath(path) { assert.equal(path, '/local/test1.txt'); return `uri:${path}`; }});
  const clipboard = host('clipboard', {
    createData(type, value) { assert.equal(type, 'uri'); assert.equal(value, 'uri:/local/test1.txt'); return pasteData; },
    getSystemPasteboard() { return board; }
  });
  Object.defineProperty(clipboard, 'MIMETYPE_TEXT_URI', {get() { event('mime'); return 'uri'; }});
  const log = host('log', {
    info(...args) { event('info', ...args); }, error(...args) { event('error', ...args); }
  });
  const globals = {ViewPU: class {finalizeConstruction() {}}, AppStorage: {get() { return ui; }}};
  for (const name of ['NavDestination', 'Column', 'Row', 'Text', 'Progress', 'PasteButton', 'Button', 'SymbolGlyph', 'If']) {
    const component = new Proxy({}, {get(_object, key) { return function(...args) {
      assert.equal(this, component);
      if (name === 'NavDestination' && key === 'create') args[0]();
      if (name === 'Button' && key === 'onClick') handlers.push(args[0]);
    }; }});
    globals[name] = component; labels.set(component, name);
  }
  const context = vm.createContext(globals);
  const dependencies = new Map();
  const values = {
    '@ohos:pasteboard': {default: clipboard}, '@ohos:file.fs': {default: io},
    '@ohos:file.fileuri': {default: uri}, '@ohos:hilog': {default: log},
    '@bundle:com.example.pasteboard/entry/ets/common/CommonConstants': {CommonConstants: {FULL_PERCENT: '100%'}}
  };
  const module = new vm.SourceTextModule(source, {context});
  await module.link(specifier => {
    const exports = values[specifier]; assert.ok(exports, specifier);
    if (!dependencies.has(specifier)) dependencies.set(specifier, new vm.SyntheticModule(Object.keys(exports), function() {
      for (const [key, value] of Object.entries(exports)) this.setExport(key, value);
    }, {context}));
    return dependencies.get(specifier);
  });
  await module.evaluate();
  const instance = Object.create(module.namespace.ProgressBar.prototype);
  instance.observeComponentCreation2 = function(callback, type) {
    assert.equal(this, instance); registrations.push({callback, name: labels.get(type)});
  };
  Object.defineProperty(instance, 'curBp', {value: 'sm'});
  const prompt = host('prompt', {showToast(value) {
    assert.deepEqual(JSON.parse(JSON.stringify(value)), {message: {id: 16777226, type: 10003, params: [],
      bundleName: 'com.example.pasteboard', moduleName: 'entry'}, offset: {dx: 0, dy: -140}});
  }});
  const callbackUi = host('callbackUi', {getPromptAction() { return prompt; }});
  instance.getUIContext = function() { assert.equal(this, instance); event('getUIContext'); return callbackUi; };
  instance.initialRender();
  const navigation = registrations.find(value => value.name === 'NavDestination'); assert.ok(navigation);
  navigation.callback(1, true);
  const button = registrations.filter(value => value.name === 'Button'); assert.equal(button.length, 1);
  button[0].callback(2, true); assert.equal(handlers.length, 1);
  active = true;
  let pending;
  try { pending = handlers[0].call({alien: true}); assert.equal(typeof pending.then, 'function'); }
  catch (error) { assert.fail('async callback threw synchronously'); }
  events.push(['invoked']);
  try { assert.equal(await pending, undefined); events.push(['resolved']); }
  catch (error) { assert.equal(error, failure); events.push(['rejected']); }
  return events;
}
try {
  const output = join(temporary, 'output');
  const run = spawnSync(tool, [join(root, 'modules.abc'), output, '--value-source'], {encoding: 'utf8'});
  assert.equal(run.status, 0, run.stderr);
  const record = JSON.parse(readFileSync(join(output, 'manifest.json'), 'utf8')).records.find(value => value.entity === 9550);
  assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, false);
  const generated = readFileSync(join(output, record.source), 'utf8');
  // The selected host path is consumed by the native concatenation. This
  // representation check is separate from the effect/throw/async oracle.
  // The common postfix printer now preserves the native optional chain.
  // Inspect language structure independently of synthetic local spellings:
  // optional context access, mandatory host call/files read, then concatenation.
  const work = [parseJavaScript(generated)], paths = [];
  while (work.length) {
    const node = work.pop();
    if (node.type === 'BinaryExpression' && node.operator === '+' && node.right.value === '/test1.txt') paths.push(node);
    for (const value of Object.values(node)) {
      if (Array.isArray(value)) {
        for (const child of value) if (child && typeof child.type === 'string') work.push(child);
      } else if (value && typeof value.type === 'string') work.push(value);
    }
  }
  assert.equal(paths.length, 1);
  assert.equal(paths[0].left.type, 'ChainExpression');
  const files = paths[0].left.expression;
  assert.equal(files.type, 'MemberExpression'); assert.equal(files.property.name, 'filesDir'); assert.equal(files.optional, false);
  assert.equal(files.object.type, 'CallExpression'); assert.equal(files.object.optional, false);
  const host = files.object.callee;
  assert.equal(host.type, 'MemberExpression'); assert.equal(host.property.name, 'getHostContext'); assert.equal(host.optional, true);
  const paired = stripTypeScriptTypes(readFileSync(join(root, 'pasteboard_src/src/main/ets/pages/ProgressBar.ts'), 'utf8'));
  let scenarios = 0;
  for (const mode of ['resolve', 'reject', 'thenable']) for (const replace of [false, true]) {
    for (let failAt = 0; failAt <= 40; ++failAt) {
      assert.deepEqual(await trace(generated, mode, replace, failAt), await trace(paired, mode, replace, failAt),
        `${mode} replace=${replace} failure=${failAt}`);
      ++scenarios;
    }
  }
  console.log(`Paired ProgressBar copy callback: ${scenarios} reference/effect/async scenarios passed`);
} finally { rmSync(temporary, {recursive: true, force: true}); }
