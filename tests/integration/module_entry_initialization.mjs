import assert from 'node:assert/strict';
import {execFileSync, spawnSync} from 'node:child_process';
import vm from 'node:vm';

// Decode a genuine module entry; the oracle supplies only observable outcomes.
// An entry declaration must precede conditional assignments, not hoist them.
async function evaluate(source, expected) {
  const root = new vm.SourceTextModule(source);
  await root.link(() => { throw new Error('unexpected import'); });
  assert.throws(() => root.namespace.answer, {name: 'ReferenceError'});
  await root.evaluate({timeout: 1000});
  assert.deepEqual(Object.keys(root.namespace), ['answer']);
  assert.equal(root.namespace.answer, expected);
}

let executions = 0;
let mutations = 0;
let refusals = 0;
for (const profile of ['1202', '1206', '1301']) {
  for (const initial of [0, 1]) {
    const expected = initial ? 7 : 9;
    const generated = execFileSync(process.argv[2], [
      `stored-branch-${initial ? 'one' : 'zero'}`, profile,
    ], {encoding: 'utf8'});
    assert.equal((generated.match(/let answer =/g) ?? []).length, 1);
    assert.ok(generated.indexOf(`let answer = ${initial};`) < generated.indexOf('if ('));
    assert.match(generated, /if \(/);
    assert.match(generated, /\(answer = 7\)/);
    assert.match(generated, /\(answer = 9\)/);
    assert.ok(!generated.includes('INCOMPLETE'));
    await evaluate(generated, expected);
    ++executions;

    for (const mutation of [
      generated.replace(`(answer = ${expected})`, `(answer = ${initial})`),
      generated.replaceAll('(answer = 7)', '(void 0)').replaceAll('(answer = 9)', '(void 0)'),
    ]) {
      assert.notEqual(mutation, generated);
      await assert.rejects(evaluate(mutation, expected), {code: 'ERR_ASSERTION'});
      ++mutations;
    }
    // A declaration after a join or arbitrary pure splitting must retain the
    // same initialized lifetime. Loop writes use the same dominance rule.
    for (const [prefix, count] of [
      ['joined-branch', 1], ['joined-copy-branch', 1], ['joined-loop', 1],
      ...[1, 200, 201, 400, 800].map(count => ['split-branch', count]),
    ]) {
      const loop = prefix === 'joined-loop';
      const source = execFileSync(process.argv[2], [
        `${prefix}-${initial ? 'one' : 'zero'}`, profile, String(count),
      ], {encoding: 'utf8'});
      assert.equal((source.match(/let answer =/g) ?? []).length, 1);
      assert.match(source, loop ? /while \(/ : /if \(/);
      assert.ok(!source.includes('INCOMPLETE'));
      await evaluate(source, loop ? 0 : expected); ++executions;
      if (!loop || initial === 1) {
        const changed = loop ? source.replace('while (true)', 'while (false)') :
          source.replace(`(answer = ${expected})`, `(answer = ${initial})`);
        assert.notEqual(changed, source); new vm.SourceTextModule(changed);
        await assert.rejects(evaluate(changed, loop ? 0 : expected), {code: 'ERR_ASSERTION'}); ++mutations;
      }
    }
  }
  for (const mode of ['conditional-first', 'branch-before-root']) {
    const result = spawnSync(process.argv[2], [mode, profile], {encoding: 'utf8'});
    assert.equal(result.status, 1);
    assert.equal(result.stdout, '');
    assert.match(result.stderr, /record error/);
    ++refusals;
  }
}
console.log(`entry module initialization: ${executions} decoded ESM executions, ${mutations} valid-syntax mutations, ${refusals} declaration refusals`);
