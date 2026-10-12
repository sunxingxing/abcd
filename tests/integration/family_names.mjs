import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import vm from 'node:vm';

const [fixture] = process.argv.slice(2);
const rows = [];
for (const name of ['restoredValue', '$arg0', '$local0', '$binding0', 'ClassValue', 'MethodSelfValue'])
  for (const space of ['global', 'module']) for (const hint of ['plain', 'named'])
    for (const layout of ['straight', 'split']) {
      const result = spawnSync(fixture, [name, space, hint, layout], {encoding: 'utf8', timeout: 60000});
      assert.equal(result.status, 0, result.stderr);
      const source = result.stdout;
      assert.doesNotMatch(source, /INCOMPLETE|UNRECOVERED|\.call\(/);
      rows.push({name, space, source});
    }

const formalRows = [];
for (const formal of ['$arg0', '$value0', '$local1', 'ClassValue', 'MethodSelfValue', 'ordinary', 'eval', 'await', 'bad-name', 'restoredValue'])
  for (const space of ['global', 'module']) for (const hint of ['plain', 'named'])
    for (const layout of ['straight', 'split']) {
      const result = spawnSync(fixture, ['restoredValue', space, hint, layout, formal], {encoding: 'utf8', timeout: 60000});
      assert.equal(result.status, 0, result.stderr);
      formalRows.push({name: 'restoredValue', formal, space, source: result.stdout});
    }
const partialRows = [];
for (const debugSet of ['sparse', 'invalid', 'duplicate'])
  for (const space of ['global', 'module']) for (const hint of ['plain', 'named'])
    for (const layout of ['straight', 'split']) {
      const result = spawnSync(fixture, ['restoredValue', space, hint, layout, 'ordinary', debugSet], {encoding: 'utf8', timeout: 60000});
      assert.equal(result.status, 0, result.stderr);
      partialRows.push({name: 'restoredValue', formal: 'ordinary', space, source: result.stdout, debugSet});
    }
async function load({name, space, source, formal, debugSet}) {
  const trace = [], sentinel = {};
  let stop = null;
  const event = kind => {trace.push(kind); if (stop === kind) throw sentinel;};
  const fn = function(value) {assert.equal(this, undefined); event('call'); return value;};
  Object.defineProperty(fn, 'call', {get() {throw new Error('callee.call');}});
  const context = vm.createContext({});
  let current = fn;
  if (space === 'global') {
    // Define the getter inside the realm. A getter on Node's sandbox object
    // can turn its thrown value into ReferenceError during context lookup.
    context.readBinding = () => {event('get'); return current;};
    vm.runInContext(`Object.defineProperty(globalThis, ${JSON.stringify(name)}, {configurable: true, get() {return readBinding();}})`, context);
  }
  const code = (space === 'module' ? `import {fn as ${name}} from 'dep';\n` : '') + `export function reference(value) {return (0, ${name})(value);}
export default (${source});`;
  const module = new vm.SourceTextModule(code, {context});
  const dependency = new vm.SyntheticModule(['fn'], function() {this.setExport('fn', fn);}, {context});
  await module.link(specifier => {assert.equal(specifier, 'dep'); return dependency;});
  await module.evaluate();
  assert.deepEqual(trace, []);
  const outer = module.namespace.default;
  assert.equal(outer.name, 'outer'); assert.equal(outer.length, debugSet ? 3 : 1);
  if (formal) {
    const header = outer.toString().split('{', 1)[0];
    const parameter = header.slice(header.indexOf('(') + 1, header.lastIndexOf(')'));
    const names = parameter.split(',').map(name => name.trim());
    if (debugSet) {
      assert.equal(names.length, 3);
      if (debugSet === 'duplicate') assert.notEqual(names[0], formal);
      else assert.equal(names[0], formal);
      assert.notEqual(names[1], name); assert.notEqual(names[1], 'bad-name'); assert.notEqual(names[1], formal);
    } else if (['eval', 'await', 'bad-name', name].includes(formal)) assert.notEqual(parameter, formal);
    else assert.equal(parameter, formal);
  }
  return {outer, reference: module.namespace.reference, trace, sentinel, setStop(value) {stop = value;}, replace(value) {
    if (space === 'module') dependency.setExport('fn', value);
    else current = value;
  }};
}
let behaviorCases = 0;
async function exercise(row) {
  const loaded = await load(row);
  const values = [undefined, null, false, 0, -0, NaN, 1n, Symbol('value'), {}, []];
  const children = values.map((value, i) => loaded.outer(value, values[(i + 1) % values.length], values[(i + 2) % values.length]));
  assert.equal(new Set(children).size, values.length);
  assert.deepEqual(loaded.trace, []); // Creating a closure must not read its callee.
  for (let i = 0; i < values.length; ++i) {
    assert.equal(children[i].name, 'child'); assert.equal(children[i].length, 0);
    const before = loaded.trace.length;
    let actual; assert.doesNotThrow(() => {actual = children[i]();});
    assert.deepEqual(loaded.trace.slice(before), row.space === 'global' ? ['get', 'call'] : ['call']);
    assert.ok(Object.is(actual, values[i])); ++behaviorCases;
  }
  for (const point of row.space === 'global' ? ['get', 'call'] : ['call']) {
    loaded.setStop(point);
    const nativeStart = loaded.trace.length;
    assert.throws(() => loaded.reference(values[0]), error => error === loaded.sentinel);
    const expected = loaded.trace.slice(nativeStart);
    const before = loaded.trace.length;
    assert.throws(() => children[0](), error => error === loaded.sentinel);
    // Contextified global lookup may revisit a throwing getter. Compare with
    // the independent native function in the same realm; never hardcode its
    // embedding behavior or blame repeated host lookup on generated source.
    assert.deepEqual(loaded.trace.slice(before), expected);
    ++behaviorCases;
  }
  loaded.setStop(null);
  const changed = function(value) {assert.equal(this, undefined); return {value};};
  loaded.replace(changed);
  for (let i = 0; i < values.length; ++i) {assert.ok(Object.is(children[i]().value, values[i])); ++behaviorCases;}
}
for (const row of [...rows, ...formalRows, ...partialRows]) await exercise(row);
const verifiedBehaviorCases = behaviorCases;

let mutationChecks = 0;
for (const row of rows.filter((_, index) => index % 2 === 0)) {
  // The captured argument copy may print as an assignment statement or, after
  // declaration recovery, as the cell's own `let` initializer.
  const assignment = row.source.match(/\((\$local\d+|\$binding\d+) = (\$arg\d+|\$binding\d+)\);/) ??
      row.source.match(/let (\$local\d+|\$binding\d+) = (\$arg\d+|\$binding\d+);/);
  assert.ok(assignment, row.source);
  const nulled = assignment[0].startsWith('let ') ? `let ${assignment[1]} = null;` : `(${assignment[1]} = null);`;
  for (const changed of [row.source.replace(assignment[0], nulled),
    row.source.replaceAll(assignment[1], row.name)]) {
    assert.notEqual(changed, row.source);
    new vm.SourceTextModule(`export default (${changed});`);
    let detected = false;
    try {await exercise({...row, source: changed});}
    catch (error) {if (!(error instanceof assert.AssertionError)) throw error; detected = true;}
    assert.equal(detected, true); ++mutationChecks;
  }
}
// A valid source mutation deliberately shadows the child's actual global or
// module callee with an otherwise unused sibling formal. Also test the restored
// first formal when it is unique. Independent traces/identity must detect both.
for (const row of partialRows) {
  const header = row.source.match(/function(?:\s+[A-Za-z_$][\w$]*)?\(([^)]*)\)/);
  assert.ok(header);
  const parameters = header[1].split(',').map(name => name.trim());
  assert.equal(parameters.length, 3);
  parameters[1] = row.name;
  const changes = [row.source.replace(header[1], parameters.join(', '))];
  if (row.debugSet !== 'duplicate') changes.push(row.source.replaceAll('ordinary', row.name));
  for (const source of changes) {
    assert.notEqual(source, row.source);
    new vm.SourceTextModule(`export default (${source});`);
    let detected = false;
    try {await exercise({...row, source});}
    catch (error) {if (!(error instanceof assert.AssertionError)) throw error; detected = true;}
    assert.equal(detected, true); ++mutationChecks;
  }
}
console.log(JSON.stringify({decodedFamilies: rows.length + formalRows.length + partialRows.length, formalFixtures: formalRows.length, partialFixtures: partialRows.length, behaviorCases: verifiedBehaviorCases, mutationChecks, wholeRecordBehavior: false}));
