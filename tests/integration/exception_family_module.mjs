import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { SourceTextModule, SyntheticModule } from 'node:vm';
import { checkQueryDegree, checkRecover } from './exception_family_execution.mjs';

const [tool, sample] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(sample)).digest('hex'),
  '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a');
for (const [method, methods, count, execute] of [
  [8257, [8257, 7810, 8118], 145, checkRecover],
  [8404, [8404, 7978], 90, checkQueryDegree],
]) {
  const result = JSON.parse(execFileSync(tool, [sample, String(method), '--emit-family'], { encoding: 'utf8' }));
  assert.equal(result.fragment_complete, true);
  assert.equal(result.record_complete, false);
  assert.deepEqual(result.members.map(member => member.method), methods);
  assert.equal(result.members.reduce((sum, member) => sum + member.positions.instruction_sources.length, 0), count);
  const modules = new Map(['sensor', 'hilog', 'window'].map(name => [name,
    new SourceTextModule('let value; export { value as default }; export function set(x) { value = x; }')]));
  const entry = new SourceTextModule(`${result.imports}\nexport default class { ${result.source} }`);
  await entry.link(specifier => {
    for (const [name, module] of modules) if (specifier === `@ohos:${name}`) return module;
    assert(['@ohos:app.ability.AbilityConstant', '@ohos:display'].includes(specifier));
    return new SyntheticModule(['default'], function () { this.setExport('default', {}); });
  });
  await entry.evaluate();
  // Rebind native live imports between cases. No injected lexical globals or
  // source rewriting may supply the recovered members' module reads.
  execute(result.source, (source, bindings) => {
    assert.equal(source, result.source);
    for (const [name, module] of modules) module.namespace.set(bindings[name]);
    return entry.namespace.default;
  });
}
