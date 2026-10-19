import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync} from 'node:fs';
import {pathToFileURL} from 'node:url';
const require = createRequire(import.meta.url);
// Same pinned, MIT-licensed Node-vendored lexer as class_reference.mjs.
const acorn = require('internal/deps/acorn/acorn/dist/acorn');
assert.equal(acorn.version, '8.15.0');
function scan(source) {
  const comments = [];
  const options = {ecmaVersion: 'latest', sourceType: 'module', onComment: comments, preserveParens: true};
  const ast = acorn.parse(source, options), grouping = new Map();
  const pending = [ast];
  while(pending.length) {
    const node = pending.pop();
    if(node.type === 'ParenthesizedExpression') grouping.set(node.start,node.end-1);
    for(const value of Object.values(node)) {
      if(Array.isArray(value)) { for(const child of value) if(child && typeof child.type === 'string') pending.push(child); }
      else if(value && typeof value.type === 'string') pending.push(value);
    }
  }
  const tokens = [...acorn.tokenizer(source, {ecmaVersion: 'latest', sourceType: 'module'})];
  return {tokens, comments, grouping};
}
// Removals are exact UTF-16 offsets from the old source, one [open, close]
// pair per grouping decision. The lexer, not character scanning, establishes
// token identity. Literal spelling and comments must remain byte-identical.
export function checkGroupingTokens(oldSource, newSource, removals) {
  const old = scan(oldSource), next = scan(newSource);
  const pairs = new Map(), stack = [];
  for (const token of old.tokens) {
    if (token.type.label === '(') stack.push(token.start);
    else if (token.type.label === ')') {
      assert(stack.length, 'unbalanced token stream');
      pairs.set(stack.pop(), token.start);
    }
  }
  assert.equal(stack.length, 0);
  const removed = new Set();
  for (const [open, close] of removals) {
    assert.equal(pairs.get(open), close, 'not an original matching parenthesis pair');
    assert.equal(old.grouping.get(open), close, 'not a grouping-expression pair');
    assert(!removed.has(open) && !removed.has(close), 'duplicate removal');
    removed.add(open); removed.add(close);
  }
  const kept = old.tokens.filter(t => !removed.has(t.start));
  assert.equal(kept.length, next.tokens.length, 'unplanned token insertion/removal');
  for (let i = 0; i < kept.length; ++i) {
    assert.equal(kept[i].type.label, next.tokens[i].type.label, `token kind at ${i}`);
    assert.equal(oldSource.slice(kept[i].start, kept[i].end),
      newSource.slice(next.tokens[i].start, next.tokens[i].end), `token spelling at ${i}`);
  }
  let oldEnd=0,newEnd=0;
  for(let i=0;i<=kept.length;++i) {
    const oldStart=i<kept.length?kept[i].start:oldSource.length;
    const newStart=i<next.tokens.length?next.tokens[i].start:newSource.length;
    let expected='', touched=false;
    for(let at=oldEnd;at<oldStart;++at) {
      if(removed.has(at)) touched=true; else expected+=oldSource[at];
    }
    const gap=newSource.slice(newEnd,newStart);
    if(gap!==expected) {
      assert(touched,'whitespace changed outside removed grouping');
      assert.equal(gap.replace(/[ \t]/g,''),expected.replace(/[ \t]/g,''),'line terminator or comment changed');
    }
    if(i<kept.length) {oldEnd=kept[i].end;newEnd=next.tokens[i].end;}
  }
  assert.deepEqual(old.comments.map(c => oldSource.slice(c.start,c.end)),
    next.comments.map(c => newSource.slice(c.start,c.end)), 'comment change');
  // Comments must also stay between the same retained tokens, not merely keep
  // their contents (moving a line comment can change ASI).
  const boundaries = (comments, tokens) => {
    let i = 0; return comments.map(c => { while(i < tokens.length && tokens[i].end <= c.start) ++i; return i; });
  };
  assert.deepEqual(boundaries(old.comments,kept),boundaries(next.comments,next.tokens));
  return {removedPairs: removals.length, tokens: kept.length};
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const [oldPath,newPath,planPath] = process.argv.slice(2);
  assert(oldPath && newPath && planPath, 'usage: node --expose-internals grouping_tokens.mjs OLD NEW PLAN.json');
  console.log(JSON.stringify(checkGroupingTokens(readFileSync(oldPath,'utf8'),readFileSync(newPath,'utf8'),JSON.parse(readFileSync(planPath,'utf8')))));
}

// Infer a deletion witness from two parsed token streams. Grammar delimiters
// remain essential tokens; only ParenthesizedExpression delimiters are optional.
export function inferGroupingRemovals(oldSource, newSource) {
  function indexed(source) {
    const {tokens,grouping}=scan(source), optional=new Set();
    for(const [a,b] of grouping){optional.add(a);optional.add(b);}
    const ordinal=new Map(), essential=[];
    for(const t of tokens){ordinal.set(t.start,essential.length);if(!optional.has(t.start))essential.push([t.type.label,source.slice(t.start,t.end)]);}
    const ranges=new Map();
    for(const [a,b] of grouping){const key=`${ordinal.get(a)}:${ordinal.get(b)}`;if(!ranges.has(key))ranges.set(key,[]);ranges.get(key).push([a,b]);}
    return {essential,ranges};
  }
  const old=indexed(oldSource), next=indexed(newSource);
  assert.deepEqual(old.essential,next.essential,'non-grouping token change');
  for(const [key,pairs] of next.ranges)assert(pairs.length<=(old.ranges.get(key)?.length??0),'new or moved grouping');
  const removed=[];
  for(const [key,pairs] of old.ranges)removed.push(...pairs.slice(next.ranges.get(key)?.length??0));
  checkGroupingTokens(oldSource,newSource,removed);
  return removed;
}

// Slice 2 only: a call-callee ParenthesizedExpression containing exactly
// Sequence(0, Identifier), never eval/member/optional/new/tag. No other token
// changes are permitted. Module parsing excludes with environments.
export function checkValueCallRemovals(oldSource, newSource) {
  const ast=acorn.parse(oldSource,{ecmaVersion:'latest',sourceType:'module',preserveParens:true});
  const candidates=new Map(), pending=[ast];
  while(pending.length) {
    const n=pending.pop();
    if(n.type==='CallExpression'&&!n.optional&&n.callee.type==='ParenthesizedExpression') {
      const p=n.callee,e=p.expression;
      if(e.type==='SequenceExpression'&&e.expressions.length===2&&e.expressions[0].type==='Literal'&&
          e.expressions[0].raw==='0'&&e.expressions[1].type==='Identifier'&&e.expressions[1].name!=='eval')
        candidates.set(p.start,{end:p.end-1,name:e.expressions[1].start});
    }
    for(const v of Object.values(n))if(Array.isArray(v)){for(const c of v)if(c?.type)pending.push(c);}else if(v?.type)pending.push(v);
  }
  const old=scan(oldSource),next=scan(newSource), removed=new Set();let j=0,count=0;
  for(let i=0;i<old.tokens.length;++i) {
    const t=old.tokens[i];if(removed.has(t.start))continue;
    const c=candidates.get(t.start);
    if(c&&next.tokens[j]?.type.label!=='(') {
      while(old.tokens[i].start<c.name){removed.add(old.tokens[i].start);++i;}
      removed.add(c.end);++count;
    }
    const a=old.tokens[i],b=next.tokens[j++];
    assert(b&&a.type.label===b.type.label&&oldSource.slice(a.start,a.end)===newSource.slice(b.start,b.end),'non-wrapper token change');
  }
  assert.equal(j,next.tokens.length);
  const kept=old.tokens.filter(t=>!removed.has(t.start));
  const deleted=old.tokens.filter(t=>removed.has(t.start));let di=0;
  let aend=0,bend=0;
  for(let i=0;i<=kept.length;++i){
    const a=i<kept.length?kept[i].start:oldSource.length,b=i<next.tokens.length?next.tokens[i].start:newSource.length;
    let gap=oldSource.slice(aend,a),touched=false;
    const local=[];while(di<deleted.length&&deleted[di].end<=a)local.push(deleted[di++]);
    for(const token of local.reverse()){
      gap=gap.slice(0,token.start-aend)+gap.slice(token.end-aend);touched=true;
    }
    const actual=newSource.slice(bend,b);
    if(gap!==actual){assert(touched,'unrelated whitespace change');assert.equal(gap.replace(/[ \t]/g,''),actual.replace(/[ \t]/g,''));}
    if(i<kept.length){aend=kept[i].end;bend=next.tokens[i].end;}
  }
  assert.deepEqual(old.comments.map(c=>oldSource.slice(c.start,c.end)),next.comments.map(c=>newSource.slice(c.start,c.end)));
  return count;
}
