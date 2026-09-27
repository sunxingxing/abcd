import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { SourceTextModule } from 'node:vm';

const result = JSON.parse(execFileSync(process.argv[2], ['--module-json'], { encoding: 'utf8' }));
assert(result.positions.module_binding);
const recovered = new SourceTextModule(`${result.imports}
export function read() { return new (class { ${result.source} })().run(); }
`);
// The dependency calls the decoded method before its own exported let has
// initialized. A genuine cyclic ESM TDZ must enter the recovered catch.
const dependency = new SourceTextModule(`
import { read } from 'entry';
export const early = read();
export let run;
export function set(value) { run = value; }
`);
await recovered.link(specifier => {
  if (specifier === 'dep') return dependency;
  assert.equal(specifier, 'entry'); return recovered;
});
await recovered.evaluate();
assert(dependency.namespace.early instanceof ReferenceError);
for (const value of [undefined, null, false, -0, NaN, 2n, Symbol('value'), {}, new Proxy({}, {})]) {
  dependency.namespace.set(value);
  assert(Object.is(recovered.namespace.read(), value));
}
const map = result.positions;
const bytes = Buffer.from(result.source);
function pcs(id) {
  const origin = map.origins[id];
  if ('pc' in origin) return [origin.pc];
  if (origin.kind === 'combined')
    return map.origin_parts.slice(origin.first_part, origin.first_part + origin.part_count).flatMap(pcs);
  return [];
}
const expected = [0, 2, 6, 7];
assert.equal(map.operations.length, expected.length);
for (const placement of map.operations) {
  const span = placement.kind === 'anchor' ? map.anchors[placement.node] :
    placement.kind === 'statement' ? map.statements[placement.node] : map.expressions[placement.node];
  assert(pcs(span.origin).includes(expected[placement.operation]));
  if (placement.kind === 'anchor') assert(span.byte <= bytes.length);
  else assert(span.begin < span.end && span.end <= bytes.length);
}
