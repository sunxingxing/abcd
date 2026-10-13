import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import vm from 'node:vm';

const [structure, fixtures] = process.argv.slice(2);
const directory = mkdtempSync(join(tmpdir(), 'abcd-constructor-forwarding-'));
let serial = 0;
function exportRecord(flags, name = 'fields') {
  const output = join(directory, String(serial++));
  const run = spawnSync(structure, [join(fixtures, name + '.abc'), output, '--value-source', ...flags], {encoding: 'utf8'});
  assert.equal(run.status, 0, run.stderr);
  const records = JSON.parse(readFileSync(join(output, 'manifest.json'), 'utf8')).records.filter(r => r.source_status !== 2);
  assert.equal(records.length, 1);
  return {record: records[0], output};
}
async function observe(source, {mode = 'setter', count = 3, alternate = false, failAt = ''} = {}) {
  const trace = [], failure = Symbol('failure'), x = {}, y = {};
  const args = Array.from({length: count}, (_, i) => i % 2 ? undefined : {i});
  let C, requested, actual;
  const event = (name, ...parts) => {trace.push([name, ...parts]); if (name === failAt) throw failure;};
  function Base(...received) {
    assert.equal(new.target, requested);
    assert.equal(received.length, args.length);
    for (let i = 0; i < args.length; ++i) assert.ok(Object.is(received[i], args[i]));
    event('base', received.length, new.target === C ? 'derived' : 'alternate');
    actual = this;
    if (mode === 'object' || mode === 'proxy-object') {
      actual = {};
      if (mode === 'proxy-object') actual = new Proxy(actual, {
        set(target, key, value) {event('set-trap', key); return Reflect.set(target, key, value);},
        defineProperty(target, key, descriptor) {event('define-trap', key); return Reflect.defineProperty(target, key, descriptor);},
      });
      return actual;
    }
    return 7;
  }
  const setters = {
    x: {configurable: true, set(value) {assert.equal(this, actual); assert.equal(value, x); event('set-x');}},
    y: {configurable: true, set(value) {assert.equal(this, actual); assert.equal(value, y); event('set-y');}},
  };
  function Target() {}
  if (mode === 'setter') {
    Object.defineProperties(Base.prototype, setters);
    Object.defineProperties(Target.prototype, setters);
  }
  if (mode === 'readonly') {
    Object.defineProperty(Base.prototype, 'x', {value: 1, writable: false});
    Object.defineProperty(Target.prototype, 'x', {value: 1, writable: false});
  }
  const initialize = (name, receiver) => {
    assert.equal(receiver, actual);
    event('init-' + name);
    return name === 'x' ? x : y;
  };
  const context = vm.createContext({});
  const root = new vm.SourceTextModule(source, {context});
  await root.link(name => {
    assert.ok(name === 'base' || name === 'initialize');
    return new vm.SyntheticModule(['default'], function() {this.setExport('default', name === 'base' ? Base : initialize);}, {context});
  });
  await root.evaluate(); C = root.namespace.default;
  assert.equal(C.length, 0);
  const parent = mode === 'proxy-parent' ? new Proxy(Base, {construct(target, list, newTarget) {
    event('construct-trap', list.length); return Reflect.construct(target, list, newTarget);
  }}) : Base;
  // Native super must resolve the current parent at invocation, and preserve
  // new.target even though the constructor does not directly observe that slot.
  Object.setPrototypeOf(C, parent);
  requested = alternate ? Target : C;
  let outcome = 'return', value;
  try {value = Reflect.construct(C, args, requested);}
  catch (error) {outcome = error === failure ? 'sentinel' : error.name;}
  if (outcome === 'return') assert.equal(value, actual);
  const properties = actual ? Reflect.ownKeys(actual).map(key => {
    const d = Object.getOwnPropertyDescriptor(actual, key);
    return [key, d.value === x ? 'x' : d.value === y ? 'y' : d.value, d.writable, d.enumerable, d.configurable];
  }) : [];
  return {trace, outcome, properties};
}
try {
  const {record, output} = exportRecord(['--language-source']);
  assert.equal(record.source_complete, true, JSON.stringify(record));
  const source = readFileSync(join(output, record.source), 'utf8');
  const oracle = readFileSync(join(fixtures, 'oracle.mjs'), 'utf8');
  assert.match(source, /constructor\(\.\.\./);
  assert.match(source, /super\(\.\.\./);
  for (const flags of [[], ['--language-arithmetic'], ['--language-numeric'], ['--language-constructors'],
    ['--language-arithmetic', '--language-constructors'], ['--language-numeric', '--language-constructors']]) {
    const previous = exportRecord(flags).record;
    assert.equal(previous.source_status, 3, JSON.stringify(previous));
    const causes = previous.source_failure?.causes ?? previous.source_causes;
    assert.ok(causes.some(cause => cause.domain.endsWith('DerivedProjectionError') && cause.value === 1), JSON.stringify(previous));
  }
  const pureOld = exportRecord([], 'pure'), pureNew = exportRecord(['--language-source'], 'pure');
  assert.equal(pureOld.record.source_complete, true);
  assert.equal(pureNew.record.source_complete, true);
  const implicit = readFileSync(join(pureOld.output, pureOld.record.source), 'utf8');
  assert.equal(readFileSync(join(pureNew.output, pureNew.record.source), 'utf8'), implicit);
  assert.doesNotMatch(implicit, /constructor\(|super\(/);
  let cases = 0;
  for (const mode of ['plain', 'setter', 'readonly', 'object', 'proxy-object', 'proxy-parent'])
    for (const count of [0, 1, 3, 200, 201]) for (const alternate of [false, true])
      for (const failAt of ['', 'base', 'init-x', 'init-y', 'set-x', 'set-y', 'set-trap', 'construct-trap']) {
        const scenario = {mode, count, alternate, failAt};
        assert.deepEqual(await observe(source, scenario), await observe(oracle, scenario), JSON.stringify(scenario));
        ++cases;
      }
  const writes = [...source.matchAll(/^\s*\(this\.[xy] = .*\);$/gm)].map(match => match[0]);
  assert.equal(writes.length, 2);
  const mutants = [
    source.replace(/super\(\.\.\.[^)]+\)/, 'super()'),
    source.replace(writes[0], ''),
    source.replace(writes[0], writes[0] + writes[0]),
    source.replace(writes.join('\n'), [writes[1], writes[0]].join('\n')),
    source.replace(writes[0], writes[0].replace(/\(this\.x = (.*)\);/, 'Object.defineProperty(this, "x", {value: $1});')),
    source.replace(/("x", )this/, '$1{}'),
  ];
  for (let i = 0; i < mutants.length; ++i) {
    assert.notEqual(mutants[i], source, 'mutation not applied ' + i);
    assert.notDeepEqual(await observe(mutants[i]), await observe(oracle), 'mutation escaped ' + i);
  }
  console.log(`${cases} Node assignment-oracle scenarios; ${mutants.length} mutations detected; six previous CLI contracts refused`);
} finally {rmSync(directory, {recursive: true, force: true});}
