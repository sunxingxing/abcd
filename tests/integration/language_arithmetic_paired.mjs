import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
const [tool, root] = process.argv.slice(2);
const temporary = mkdtempSync(join(tmpdir(), 'abcd-language-arithmetic-'));
const cases = [
  {input: 'Immersive', entity: 14411, name: 'Content', paired: 'Immersive_src/minigame/src/main/ets/view/Content.ts'},
  {input: 'MultiCommunityApplication', entity: 45391, name: 'PictureDetail', paired: 'src/picture/src/main/ets/view/PictureDetail.ts'}
];
let scenarios = 0;
async function load(source, name) {
  const registrations = [], keys = [];
  class ViewPU {}
  const globals = {ViewPU};
  for (const name of ['Column','Grid','GridItem','Image','Stack','Row','GridRow','GridCol','Swiper','ForEach','If','__Common__'])
    globals[name] = new Proxy({}, {get: () => function() {}});
  const context = vm.createContext(globals);
  const module = new vm.SourceTextModule(source, {context});
  await module.link(() => new vm.SyntheticModule(['BreakpointType','CommonConstants','BreakpointConstants',
      'CardArrayViewModel','CommentInputView','DetailVertical'], function() {
    for (const name of ['BreakpointType','CommonConstants','BreakpointConstants','CardArrayViewModel','CommentInputView','DetailVertical'])
      this.setExport(name, {});
  }, {context}));
  await module.evaluate();
  const C = module.namespace[name];
  const owner = Object.create(C.prototype);
  if (name === 'PictureDetail') Object.defineProperty(owner, 'picList', {value: [1,2]});
  owner.observeComponentCreation2 = function(callback, component) {
    assert.equal(this, owner); registrations.push({callback, component});
  };
  owner.forEachUpdateFunction = function(id, items, item, key, ...flags) {
    assert.equal(this, owner); assert.equal(id, 17); keys.push(key);
    assert.ok(Array.isArray(items)); assert.equal(typeof item, 'function');
  };
  owner.initialRender();
  for (const {callback, component} of registrations)
    if (component === globals.ForEach) callback.call({wrongReceiver: true}, 17, true);
  assert.equal(keys.length, 1);
  return {owner, key: keys[0]};
}
function outcome(fn) {
  try { return {thrown: false, value: fn()}; }
  catch (value) { return {thrown: true, value}; }
}
function compare(a,b) {
  assert.equal(a.thrown, b.thrown);
  if (a.value instanceof Error || Object.prototype.toString.call(a.value) === '[object Error]')
    assert.equal(a.value.name, b.value.name);
  else assert.ok(Object.is(a.value,b.value));
}
try {
  // ABC-only generation finishes before paired source is opened.
  const generated = [];
  for (const item of cases) {
    const output = join(temporary, item.input);
    execFileSync(tool, [join(root,item.input,'modules.abc'), output, ...(item.name === 'Content' ? ['--language-source', '--value-source'] : ['--value-source', '--language-source'])], {timeout: 60_000});
    const manifest = JSON.parse(readFileSync(join(output,'manifest.json')));
    assert.equal(manifest.source_contract, 'language_source_v10');
    const record = manifest.records.find(record => record.entity === item.entity);
    assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, undefined);
    const text = readFileSync(join(output,record.source), 'utf8');
    assert.doesNotMatch(text, /INCOMPLETE|switch\s*\(/); generated.push(text);
  }
  for (let i=0; i<cases.length; ++i) {
    const item = cases[i];
    const reference = stripTypeScriptTypes(readFileSync(join(root,item.input,item.paired), 'utf8'));
    const actual = await load(generated[i], item.name), expected = await load(reference, item.name);
    assert.match(actual.key.toString(), /\+/);
    for (const index of [undefined,null,false,true,0,-0,1,-1,-2147483648,2147483648,NaN,Infinity,'2',2n,Symbol('index')])
      for (const payload of [undefined,null,false,0,-0,NaN,Infinity,'image',2n,Symbol('image'),{image:true},[1,2]]) {
        compare(outcome(()=>actual.key(payload,index)), outcome(()=>expected.key(payload,index))); ++scenarios;
      }
    const token = Symbol('failure');
    for (const index of [0,-2147483648,NaN,'index',2n,Symbol('index')])
      for (const failAt of [0,1,2,3,4,5]) {
        const run = fn => {
          const events = [];
          const event = value => { events.push(value); if (events.length === failAt) throw token; };
          const payload = {get toJSON() {event('toJSON.get'); return function(key) {
            assert.equal(this, payload); event(`toJSON.call:${key}`); return {get value() {event('value.get'); return 2;}};
          };}};
          const result = outcome(()=>fn(payload,index)); return {events,result};
        };
        const a=run(actual.key), b=run(expected.key); compare(a.result,b.result); assert.deepEqual(a.events,b.events); ++scenarios;
      }
    if (item.name === 'Content') {
      assert.match(generated[i], /Array\.from/); assert.match(generated[i], /index %/);
      for (const length of [undefined,null,-1,0,1,5,7,200,201,402,1.5,'5']) {
        assert.equal(JSON.stringify(actual.owner.getBirdImages(length)), JSON.stringify(expected.owner.getBirdImages(length))); ++scenarios;
      }
    }
  }
} finally {rmSync(temporary,{recursive:true});}
console.log(JSON.stringify({pairedRecords: cases.map(item=>item.entity), pairedCallbackScenarios: scenarios,
  scope: 'registered key callbacks and Content array creation; no complete UI or constructor acceptance'}));
