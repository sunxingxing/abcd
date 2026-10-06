import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {verifyNavigationGraph} from './navigation_graph_state.mjs';

const [tool, root] = process.argv.slice(2);
const abc = join(root, 'modules.abc');
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a');
const records = [
  [9572, 'default/src/main/ets/pages/Index'],
  [8755, 'default/src/main/ets/constants/Constants'],
  [11285, 'default/src/main/ets/pages/backgroundImmersive/componentBackgroundImmersive/Shopping'],
  [11877, 'default/src/main/ets/pages/backgroundImmersive/componentPageImmersive/ShoppingAvoid'],
  [12527, 'default/src/main/ets/pages/fullScreenImmersive/componentPageImmersive/MiniGame'],
  [13504, 'default/src/main/ets/pages/fullScreenImmersive/freeformWindowImmersive/ShoppingFullScreen'],
  [6888, 'commons/Index'],
  [7167, 'commons/src/main/ets/constants/CommonConstants'],
  [7395, 'commons/src/main/ets/utils/Breakpoint'],
  [7618, 'commons/src/main/ets/utils/WindowUtil'],
];
const directory = mkdtempSync(join(tmpdir(), 'abcd-navigation-graph-'));
const sources = {};
try {
  const output = join(directory, 'output');
  execFileSync(tool, [abc, output, '--value-source', '--language-numeric', '--language-constructors'],
    {encoding: 'utf8', timeout: 60_000});
  const manifest = JSON.parse(readFileSync(join(output, 'manifest.json')));
  const byEntity = new Map(manifest.records.map(record => [record.entity, record]));
  for (const [entity, name] of records) {
    const record = byEntity.get(entity);
    assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, false);
    assert.equal(record.source_route, 'value_tree');
    assert.equal(record.source, `sources/${name}.mjs`);
    sources[name] = readFileSync(join(output, record.source), 'utf8');
  }
} finally {rmSync(directory, {recursive: true});}

// Independent reference preparation starts after the entire ABC-only exporter
// exits. Erase uninitialized typed declarations; preserve every initializer,
// constructor, method and closure. The decorator is identity in this graph's
// controlled host, so its reference erasure does not claim decorator semantics.
const reference = {};
for (const [, name] of records) {
  let text = readFileSync(join(root, 'Immersive_src', `${name}.ts`), 'utf8');
  // Match declarations only inside classes in these paired fixtures: interface
  // members need no adaptation and may stay for the independent TS parser.
  text = text.replace(/^([ \t]*)(private|public)\s+([A-Za-z_$][\w$]*)\??:\s*([^;\n=]+);[ \t]*$/gm,
    '$1declare $2 $3: $4;');
  if (name.endsWith('/Breakpoint')) text = text.replace(/^    (sm|md|lg|xl): T;$/gm, '    declare $1: T;');
  if (name.endsWith('/WindowUtil')) {
    assert.equal([...text.matchAll(/^@Observed$/gm)].length, 1);
    text = text.replace(/^@Observed$/gm, '');
  }
  reference[name] = stripTypeScriptTypes(text, {mode: 'transform'});
}
console.log(JSON.stringify(await verifyNavigationGraph(sources, reference)));
