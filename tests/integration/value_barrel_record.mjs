import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import vm from 'node:vm';

const [tool, abc, pairedRoot] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a');
// Independent dependency doubles test barrel semantics, not dependency bodies.
// Export inventories come from the paired oracle, never from recovered output.
for (const [directory, entry] of [['commons', 7000], ['minigame', 14383], ['shopping', 16219]]) {
  const reference = readFileSync(`${pairedRoot}/${directory}/Index.ts`, 'utf8');
  const inventory = new Map();
  for (const match of reference.matchAll(/export\s*\{([^}]+)\}\s*from\s*"([^"]+)"\s*;/g)) {
    assert.ok(!inventory.has(match[2]));
    inventory.set(match[2], match[1].split(',').map(name => name.trim()));
  }
  assert.ok(inventory.size > 0);
  const run = spawnSync(tool, [abc, String(entry), '--record-source', '--value-source'],
    {encoding: 'utf8', timeout: 60_000});
  assert.equal(run.status, 0, run.stderr + run.stdout);
  const report = JSON.parse(run.stdout);
  assert.equal(report.source_complete, true);
  assert.equal(report.legacy_writer_used, false);
  assert.equal(report.route, 'value_tree');
  // Representation is native ESM reexport syntax with no copied-value locals.
  assert.match(report.source, /export\s*\{/);
  assert.doesNotMatch(report.source, /\b(?:let|const|var|function)\b/);
  async function observe(source, missing = false) {
    const order = [];
    const context = vm.createContext({order});
    const dependencies = new Map();
    let id = 0;
    for (const [specifier, names] of inventory) {
      const declarations = names.map(name => {
        const identity = id++;
        return missing && identity === 0 ? '' : `export let ${name} = {id: ${identity}};`;
      }).join('\n');
      const mutation = missing ? '' : names.map(name => `${name} = {id: ${name}.id + 100};`).join('\n');
      dependencies.set(specifier, new vm.SourceTextModule(
        `${declarations}\nexport function mutate() {${mutation}}\norder.push(${JSON.stringify(specifier)});`, {context}));
    }
    const module = new vm.SourceTextModule(source, {context});
    const link = module.link(specifier => {
      assert.ok(dependencies.has(specifier), specifier);
      return dependencies.get(specifier);
    });
    if (missing) { await assert.rejects(link, {name: 'SyntaxError'}); return; }
    await link;
    const keys = [...inventory.values()].flat().sort();
    assert.deepEqual(Object.getOwnPropertyNames(module.namespace), keys);
    for (const key of keys) assert.throws(() => module.namespace[key], {name: 'ReferenceError'});
    await module.evaluate();
    assert.deepEqual(order, [...inventory.keys()]);
    const before = keys.map(key => module.namespace[key].id);
    for (const [specifier, names] of inventory) {
      const dependency = dependencies.get(specifier);
      const previous = names.map(name => module.namespace[name]);
      for (const name of names) assert.equal(module.namespace[name], dependency.namespace[name]);
      dependency.namespace.mutate();
      for (const [i, name] of names.entries()) {
        assert.equal(module.namespace[name], dependency.namespace[name]);
        assert.notEqual(module.namespace[name], previous[i]);
        assert.equal(module.namespace[name].id, previous[i].id + 100);
      }
    }
    return {keys, order, before, after: keys.map(key => module.namespace[key].id)};
  }
  assert.deepEqual(await observe(report.source), await observe(reference));
  await observe(report.source, true);
  await observe(reference, true);
}
