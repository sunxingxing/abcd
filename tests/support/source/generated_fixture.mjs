import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createHash, randomUUID} from 'node:crypto';
import {mkdirSync, readFileSync, realpathSync, renameSync, rmSync, writeFileSync} from 'node:fs';
import {dirname, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const digest = value => createHash('sha256').update(value).digest('hex');
const identity = (executable, args) => ({
  executable: realpathSync(executable), args, binary: digest(readFileSync(executable)),
});

export function produceFixture(path, executable, args, options) {
  mkdirSync(dirname(path), {recursive: true});
  // A failed/aborted producer must not leave an older successful publication.
  rmSync(path, {force: true});
  const before = identity(executable, args);
  const stdout = execFileSync(executable, args, {...options, encoding: 'utf8'});
  assert.deepEqual(identity(executable, args), before, 'producer changed during generation');
  const artifact = {version: 1, identity: before, digest: digest(stdout), stdout};
  const temporary = `${path}.${randomUUID()}.tmp`;
  try {
    writeFileSync(temporary, JSON.stringify(artifact));
    renameSync(temporary, path);
  } finally {
    rmSync(temporary, {force: true});
  }
  return {bytes: Buffer.byteLength(stdout), digest: artifact.digest};
}

// Standalone invocation still generates afresh. Under CTest, a missing, damaged
// or stale publication fails; it cannot silently substitute old source. Other
// emitter modes (scale/chains/units/phased) remain independent executions.
export function generatedSource(executable, args, options, path = process.env.ABCD_SOURCE_FIXTURE) {
  if (!path) return execFileSync(executable, args, options);
  const artifact = JSON.parse(readFileSync(path, 'utf8'));
  assert.equal(artifact.version, 1);
  assert.equal(artifact.identity.executable, realpathSync(executable));
  assert.equal(artifact.identity.binary, digest(readFileSync(executable)), 'stale producer');
  if (JSON.stringify(artifact.identity.args) !== JSON.stringify(args)) {
    return execFileSync(executable, args, options);
  }
  assert.equal(digest(artifact.stdout), artifact.digest, 'damaged generated source');
  assert.equal(options.encoding, 'utf8');
  assert.ok(Buffer.byteLength(artifact.stdout) <= options.maxBuffer, 'generated source exceeds consumer limit');
  return artifact.stdout;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const [path, maxBuffer, timeout, executable, ...args] = process.argv.slice(2);
  const result = produceFixture(path, executable, args, {maxBuffer: Number(maxBuffer), timeout: Number(timeout)});
  console.log(JSON.stringify({executable, ...result}));
}
