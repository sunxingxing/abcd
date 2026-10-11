import assert from 'node:assert/strict';
import {execFileSync, spawnSync} from 'node:child_process';
import {chmodSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {dirname, join, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {generatedSource, produceFixture} from './generated_fixture.mjs';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const directory = mkdtempSync(join(tmpdir(), 'abcd-source-fixture-'));
const binary = join(directory, 'emitter');
const artifact = join(directory, 'rows.json');
const counter = join(directory, 'count');
const options = {encoding: 'utf8', maxBuffer: 4096, timeout: 3000};
const program = `#!${process.execPath}
import {readFileSync,writeFileSync,existsSync} from 'node:fs';
const count=${JSON.stringify(counter)};
writeFileSync(count,String(Number(existsSync(count)?readFileSync(count,'utf8'):0)+1));
if(existsSync(${JSON.stringify(join(directory, 'fail'))}))process.exit(7);
if(process.argv.includes('--slow'))await new Promise(resolve=>setTimeout(resolve,10000));
console.log(JSON.stringify({source:'function f(){return 42;}',args:process.argv.slice(2)}));
`;
try {
  writeFileSync(binary, program); chmodSync(binary, 0o755);
  produceFixture(artifact, binary, ['--emit'], options);
  const expected = generatedSource(binary, ['--emit'], options, artifact);
  assert.equal(readFileSync(counter, 'utf8'), '1');
  assert.equal(JSON.parse(expected).source, 'function f(){return 42;}');
  // An unrelated emitter mode is executed, never replaced with the shared rows.
  assert.deepEqual(JSON.parse(generatedSource(binary, ['--scale'], options, artifact)).args, ['--scale']);
  assert.equal(readFileSync(counter, 'utf8'), '2');
  assert.throws(() => generatedSource(binary, ['--emit'], {...options, maxBuffer: 1}, artifact));
  const good = readFileSync(artifact, 'utf8');
  const damaged = JSON.parse(good); damaged.stdout += 'bad';
  writeFileSync(artifact, JSON.stringify(damaged));
  assert.throws(() => generatedSource(binary, ['--emit'], options, artifact), /damaged/);
  writeFileSync(artifact, good); writeFileSync(binary, program + '\n');
  assert.throws(() => generatedSource(binary, ['--emit'], options, artifact), /stale/);
  writeFileSync(binary, program);
  writeFileSync(join(directory, 'fail'), '');
  assert.throws(() => produceFixture(artifact, binary, ['--emit'], options));
  assert.throws(() => generatedSource(binary, ['--emit'], options, artifact), /ENOENT/);
  rmSync(join(directory, 'fail'));
  assert.throws(() => produceFixture(artifact, binary, ['--slow'], {...options, timeout: 50}));
  assert.throws(() => generatedSource(binary, ['--emit'], options, artifact), /ENOENT/);
  assert.throws(() => produceFixture(artifact, binary, ['--emit'], {...options, maxBuffer: 1}));
  assert.throws(() => generatedSource(binary, ['--emit'], options, artifact), /ENOENT/);

  // Exercise real CTest selection: consumer-only schedules a fresh producer on
  // every invocation; producer failure makes the consumer Not Run.
  writeFileSync(join(directory, 'consumer.mjs'), `import {generatedSource} from ${JSON.stringify(new URL('./generated_fixture.mjs', import.meta.url).href)};\nconsole.log(generatedSource(process.argv[2],['--emit'],{encoding:'utf8',maxBuffer:4096}));\n`);
  writeFileSync(join(directory, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.20)
project(FixtureContract NONE)
enable_testing()
set(ABCD_NODE_EXECUTABLE "${process.execPath}")
set(PROJECT_SOURCE_DIR "${root}")
include("${root}/tests/support/source/generated_fixture.cmake")
add_executable(emitter IMPORTED)
set_target_properties(emitter PROPERTIES IMPORTED_LOCATION "${binary}")
add_source_fixture(emitter 4096 3000 --emit)
add_test(NAME consumer COMMAND "${process.execPath}" "${directory}/consumer.mjs" "${binary}")
require_source_fixture(consumer emitter)
`);
  const build = join(directory, 'build');
  execFileSync('cmake', ['-S', directory, '-B', build], {stdio: 'pipe'});
  for (let i = 0; i < 2; ++i) {
    const before = Number(readFileSync(counter, 'utf8'));
    const output = execFileSync('ctest', ['--test-dir', build, '-R', '^consumer$', '-j', '2', '--output-on-failure'], {encoding: 'utf8'});
    assert.match(output, /2\/2/);
    assert.equal(Number(readFileSync(counter, 'utf8')), before + 1);
  }
  writeFileSync(join(directory, 'fail'), '');
  const failed = spawnSync('ctest', ['--test-dir', build, '-R', '^consumer$', '--output-on-failure'], {encoding: 'utf8'});
  assert.notEqual(failed.status, 0);
  assert.match(failed.stdout, /consumer[^\n]*Not Run/);
  console.log('fixture publication, freshness, limits, failure propagation and CTest selection passed');
} finally {
  rmSync(directory, {recursive: true, force: true});
}
