import {createHash} from 'node:crypto';
import {mkdirSync, readdirSync, readFileSync, renameSync, writeFileSync} from 'node:fs';
import {dirname, join} from 'node:path';
import {fileURLToPath} from 'node:url';

// Memoizes a pure whole-graph differential. Its verdict is a function of the
// emitted sources, the paired reference, the case parameters and the harness
// code, so identical inputs cannot change the result. The key covers every
// harness module in this directory (not just the import graph) and the Node
// version; any edit invalidates every entry. The exporter itself still runs
// on every invocation, and only a passing verdict is ever stored.
// ABCD_TEST_CACHE=off forces recomputation (milestone runs).
const here = dirname(fileURLToPath(import.meta.url));

function harnessDigest() {
  const hash = createHash('sha256');
  for (const name of readdirSync(here).filter(name => name.endsWith('.mjs')).sort())
    hash.update(name).update('\0').update(readFileSync(join(here, name))).update('\0');
  return hash.digest('hex');
}

export async function cachedVerdict({tool, name, inputs}, compute) {
  const disabled = process.env.ABCD_TEST_CACHE === 'off';
  const key = createHash('sha256').update(JSON.stringify({
    name, inputs, harness: harnessDigest(), node: process.version,
  })).digest('hex');
  const directory = join(dirname(tool), 'test-cache', name);
  const file = join(directory, key + '.json');
  if (!disabled) {
    try {
      return {...JSON.parse(readFileSync(file, 'utf8')), cached: true};
    } catch {}
  }
  const verdict = await compute();
  mkdirSync(directory, {recursive: true});
  const temporary = `${file}.${process.pid}.tmp`;
  writeFileSync(temporary, JSON.stringify(verdict));
  renameSync(temporary, file);
  return {...verdict, cached: false};
}
