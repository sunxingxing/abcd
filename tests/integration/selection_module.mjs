import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

let scenarios = 0;
for (const phased of [false, true])
for (const selection of ['conditional', 'and', 'or'])
for (const direct of [false, true]) {
  const prefix = selection === 'conditional' ? '' : `${selection}-`;
  const phasedPrefix = `phased-module-${selection === 'conditional' ? 'selection' : selection}-`;
  const option = `--${phased ? phasedPrefix : prefix}${direct ? 'direct-' : ''}source`;
  const source = execFileSync(process.argv[2], [option], {encoding: 'utf8'});
  assert.ok(!source.includes('INCOMPLETE'));
  const module = `imported${direct ? '' : '.option'}`;
  const expression = selection === 'conditional' ? `value.probe === 'on' ? ${module} : 448` :
    `value.probe ${selection === 'and' ? '&&' : '||'} ${module}`;
  const statements = `return value.method(${expression});`;
  const reference = `(function(value) { ${phased ? `try { ${statements} } catch (error) { return error; }` : statements} })`;
  const selected = probe => selection === 'conditional' ? probe === 'on' : selection === 'and' ? !!probe : !probe;
  const pair = vm.runInNewContext(`(() => { let imported; return {
    recovered: ${source}, reference: ${reference}, set(value) { imported = value; }
  }; })()`);
  function run(fn, probe, token, fail, proxy, bad = false) {
    const events = [], thrown = {}, payload = {};
    let step = 0;
    const event = kind => { events.push(kind); if (++step === fail) throw thrown; };
    const current = {get option() { event('module-property'); return token; }};
    pair.set({option: {}});
    const target = function(argument) {
      assert.equal(this, value);
      assert.equal(argument, selected(probe) ? (direct ? current : token) : selection === 'conditional' ? 448 : probe);
      event('call'); return payload;
    };
    // Native references never consult mutable .call/.apply properties.
    for (const key of ['call', 'apply']) Object.defineProperty(target, key, {get() { throw new Error('ambient call adapter'); }});
    const callable = proxy ? new Proxy(target, {apply(fn, receiver, args) {
      assert.equal(fn, target); assert.equal(receiver, value); assert.equal(args.length, 1);
      event('apply-trap'); return Reflect.apply(fn, receiver, args);
    }}) : target;
    const value = {
      get method() { event('member'); pair.set({option: {}}); return bad ? undefined : callable; },
      get probe() {
        event('predicate'); pair.set(current);
        Object.defineProperty(value, 'method', {value() { throw new Error('callee reread'); }});
        return probe;
      }
    };
    try {
      const result = fn(value);
      if (phased && result === thrown) events.push('sentinel');
      else if (phased && bad && result?.name === 'TypeError') events.push('TypeError');
      else { assert.equal(result, payload); events.push('return'); }
    }
    catch (error) {
      assert.equal(phased, false, 'caught completion escaped the recovered function');
      if (error === thrown) events.push('sentinel');
      else if (bad && error.name === 'TypeError') events.push('TypeError');
      else throw error;
    }
    return events;
  }
  const noCoercion = {[Symbol.toPrimitive]() { throw new Error('strict comparison coerced'); }};
  for (const probe of ['on', 'off', undefined, null, false, -0, NaN, 1n, Symbol(), noCoercion])
    for (const token of [undefined, -0, NaN, 1n, Symbol(), {}])
      for (const proxy of [false, true]) for (let fail = 0; fail <= 5; ++fail) {
        assert.deepEqual(run(pair.recovered, probe, token, fail, proxy), run(pair.reference, probe, token, fail, proxy));
        ++scenarios;
      }
  for (const probe of ['on', 'off']) {
    assert.deepEqual(run(pair.recovered, probe, {}, 0, false, true), run(pair.reference, probe, {}, 0, false, true));
    ++scenarios;
  }
  // Both functions are invoked before the lexical module binding is initialized.
  // The untaken arm must not touch it; a taken arm throws before invocation.
  const tdz = vm.runInNewContext(`(() => {
    const recovered = ${source}, reference = ${reference};
    const results = [];
    for (const fn of [recovered, reference]) for (const probe of ${selection === 'conditional' ? "['off', 'on']" :
        selection === 'and' ? '[false, true]' : '[true, false]'}) {
      let calls = 0;
      try { const result = fn({probe, method(argument) { ++calls; return argument; }});
        results.push([${phased} && result?.name === 'ReferenceError' ? result.name : result, calls]); }
      catch (error) { if (${phased}) throw error; results.push([error.name, calls]); }
    }
    let imported; return results;
  })()`);
  assert.equal(tdz[0][0], selection === 'conditional' ? 448 : selection === 'or'); assert.equal(tdz[0][1], 1);
  assert.equal(tdz[1][0], 'ReferenceError'); assert.equal(tdz[1][1], 0);
  assert.equal(JSON.stringify(tdz.slice(0, 2)), JSON.stringify(tdz.slice(2)));
  scenarios += 4;
  const sequence = execFileSync(process.argv[2], [option, '--twice'], {encoding: 'utf8'});
  const repeatedBody = `value.method(${expression}); return value.method(${expression});`;
  const repeated = vm.runInNewContext(`(() => { let imported; return {
    recovered: ${sequence}, reference(value) { ${phased ? `try { ${repeatedBody} } catch (error) { return error; }` : repeatedBody} },
    set(value) { imported = value; }
  }; })()`);
  function repeat(fn, probe, fail) {
    const events = [], thrown = {}, namespaces = [], argumentsSeen = [];
    let step = 0, iteration = 0;
    const event = label => { events.push(label); if (++step === fail) throw thrown; };
    const value = {
      get method() {
        event('member'); const current = ++iteration;
        return function(argument) { assert.equal(this, value); event('call'); argumentsSeen.push(argument); return current; };
      },
      get probe() {
        event('predicate'); const token = {};
        const namespace = {get option() { event('module-property'); return token; }};
        namespaces.push(direct ? namespace : token); repeated.set(namespace); return probe;
      }
    };
    try {
      const result = fn(value);
      if (phased && result === thrown) { events.push('sentinel'); return events; }
      assert.equal(result, 2);
      assert.equal(argumentsSeen.length, 2);
      for (let i = 0; i < 2; ++i) assert.equal(argumentsSeen[i], selected(probe) ? namespaces[i] :
        selection === 'conditional' ? 448 : probe);
      events.push('return');
    } catch (error) {
      assert.equal(phased, false, 'caught repeated completion escaped the recovered function');
      assert.equal(error, thrown); events.push('sentinel');
    }
    return events;
  }
  for (const probe of ['on', 'off', undefined, false, true]) for (let fail = 0; fail <= 8; ++fail) {
    assert.deepEqual(repeat(repeated.recovered, probe, fail), repeat(repeated.reference, probe, fail)); ++scenarios;
  }
}
console.log(`Selected module reads: ${scenarios} effect/receiver/identity/throw/TDZ scenarios passed.`);
