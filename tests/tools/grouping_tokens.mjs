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

// Brace-only mode: removable tokens must delimit an eligible original BlockStatement.
// The AST comparison also proves that no dangling else was reassociated.
export function checkBraceRemovals(oldSource,newSource,counts) {
  const options={ecmaVersion:'latest',sourceType:'module'};
  const commentsA=[],commentsB=[];
  const a=acorn.parse(oldSource,{...options,onComment:commentsA});
  const b=acorn.parse(newSource,{...options,onComment:commentsB});
  assert.deepEqual(commentsA.map(x=>[x.type,x.value]),commentsB.map(x=>[x.type,x.value]));
  const eligible=new Map(),alternates=new Set();
  function walk(node,parent,key) {
    if(!node||typeof node!=='object')return;
    if(node.type==='BlockStatement' && ((parent?.type==='BlockStatement'&&key==='body')||
       (parent?.type==='IfStatement'&&key==='alternate'&&node.body.length===1&&node.body[0].type==='IfStatement')) &&
       node.body.length && !node.body.some(x=>['VariableDeclaration','FunctionDeclaration','ClassDeclaration'].includes(x.type)) &&
       !(node.body[0].type==='ExpressionStatement'&&typeof node.body[0].expression.value==='string') &&
       !commentsA.some(x=>x.start>node.start&&x.end<node.end)){eligible.set(node.start,node.end-1);if(parent?.type==='IfStatement')alternates.add(node.start);}
    for(const [k,v]of Object.entries(node))if(Array.isArray(v))v.forEach(x=>walk(x,node,k));else if(v&&typeof v==='object')walk(v,node,k);
  }
  walk(a,null,'');
  const old=[...acorn.tokenizer(oldSource,options)],next=[...acorn.tokenizer(newSource,options)],removed=new Set();let j=0;
  const closes=new Set(eligible.values());
  for(const t of old){if(removed.has(t.start))continue;const n=next[j];if(n&&oldSource.slice(t.start,t.end)===newSource.slice(n.start,n.end)){++j;continue;}
    assert((t.type.label==='{'&&eligible.has(t.start))||(t.type.label==='}'&&closes.has(t.start)),'unplanned token change');removed.add(t.start);if(eligible.has(t.start))removed.add(eligible.get(t.start));}
  assert.equal(j,next.length);
  for(const [open,close]of eligible)assert.equal(removed.has(open),removed.has(close),'partial brace pair');
  function canonical(node,oldSide){
    if(Array.isArray(node))return node.flatMap(x=>oldSide&&eligible.has(x?.start)&&removed.has(x.start)?canonical(x.body,oldSide):[canonical(x,oldSide)]);
    if(!node||typeof node!=='object')return node;
    if(oldSide&&eligible.has(node.start)&&removed.has(node.start)&&node.body.length===1&&node.body[0].type==='IfStatement')return canonical(node.body[0],oldSide);
    return Object.fromEntries(Object.entries(node).filter(([k])=>!['start','end','raw'].includes(k)).map(([k,v])=>[k,canonical(v,oldSide)]));
  }
  assert.deepEqual(canonical(a,true),canonical(b,false));
  if(counts){counts.elseIf=[...alternates].filter(x=>removed.has(x)).length;counts.flattened=removed.size/2-counts.elseIf;}
  return removed.size/2;
}

// The only non-grouping rewrite in readable-v1: != and !== are the exact
// Boolean negations of == and ===. Preserve both operands, comments and order.
export function rewriteNegatedEqualities(source, newSource) {
  let selected;
  if(newSource!==undefined) {
    const parse=s=>acorn.parse(s,{ecmaVersion:'latest',sourceType:'module',preserveParens:true});
    selected=new Set();const work=[[parse(source),parse(newSource)]];
    while(work.length) {
      let [a,b]=work.pop();
      while(a?.type==='ParenthesizedExpression')a=a.expression;
      while(b?.type==='ParenthesizedExpression')b=b.expression;
      if(a?.type==='UnaryExpression'&&a.operator==='!') {
        let inner=a.argument;while(inner.type==='ParenthesizedExpression')inner=inner.expression;
        if(inner.type==='BinaryExpression'&&(inner.operator==='!='||inner.operator==='!==')&&
            b?.type==='BinaryExpression'&&b.operator===(inner.operator==='!='?'==':'===')) {
          selected.add(a.start);work.push([inner.left,b.left],[inner.right,b.right]);continue;
        }
      }
      if(a===null||b===null||typeof a!=='object'||typeof b!=='object') {assert.deepEqual(a,b,'unapproved AST change');continue;}
      if(Array.isArray(a)||Array.isArray(b)) {assert(Array.isArray(a)&&Array.isArray(b));assert.equal(a.length,b.length);for(let i=0;i<a.length;++i)work.push([a[i],b[i]]);continue;}
      const keys=x=>Object.keys(x).filter(k=>!['start','end','loc','range','raw'].includes(k)).sort();
      assert.deepEqual(keys(a),keys(b),'AST fields');for(const k of keys(a))work.push([a[k],b[k]]);
    }
  }
  const {tokens}=scan(source);
  const ast=acorn.parse(source,{ecmaVersion:'latest',sourceType:'module',preserveParens:true});
  const edits=[],pending=[ast];
  while(pending.length) {
    const n=pending.pop();let child=n.argument;
    if(n.type==='UnaryExpression'&&n.operator==='!'&&(!selected||selected.has(n.start))) {
      while(child.type==='ParenthesizedExpression')child=child.expression;
      if(child.type==='BinaryExpression'&&(child.operator==='!='||child.operator==='!==')) {
        let lo=0,hi=tokens.length;
        while(lo<hi){const mid=lo+((hi-lo)>>1);if(tokens[mid].start<child.left.end)lo=mid+1;else hi=mid;}
        const op=tokens[lo];assert(op&&op.start<child.right.start&&op.value===child.operator);
        edits.push([n.start,n.start+1,''],[op.start,op.end,child.operator==='!='?'==':'===']);
      }
    }
    for(const v of Object.values(n))if(Array.isArray(v)){for(const c of v)if(c?.type)pending.push(c);}else if(v?.type)pending.push(v);
  }
  let rewritten=source;
  for(const [a,b,text]of edits.sort((a,b)=>b[0]-a[0]))rewritten=rewritten.slice(0,a)+text+rewritten.slice(b);
  return {source:rewritten,count:edits.length/2,edits};
}
export function checkNegatedEqualityRemovals(oldSource,newSource) {
  const witness=rewriteNegatedEqualities(oldSource,newSource);
  return {removed:inferGroupingRemovals(witness.source,newSource).length,negated:witness.count,witness:witness.source};
}

export function sourceAwaitSites(source) {
 const ast=acorn.parse(source,{ecmaVersion:'latest',sourceType:'module',preserveParens:true});
 const sites=[],work=[[ast,undefined,undefined]];
 while(work.length) {
  const [n,owner,statement]=work.pop();
  const method=n.type==='MethodDefinition'?(n.key.name??n.key.value):owner;
  if(n.type==='AwaitExpression')sites.push({start:n.start,end:n.end,operand:n.argument.start,method,
    statement:statement?[statement.start,statement.end]:undefined});
  for(const v of Object.values(n)) {
   const add=child=>{if(!child?.type)return;
    const parent=n.type==='ExpressionStatement'&&child===n.expression?n:
      n.type==='ParenthesizedExpression'&&child===n.expression?statement:undefined;
    work.push([child,method,parent]);};
   if(Array.isArray(v))for(const child of v)add(child);else add(v);
  }
 }
 return sites.sort((a,b)=>a.start-b.start);
}

// Independent source witness for printer-only statement projections. The
// selected sites are found by AST correspondence, not diagnostic text/regex.
export function rewriteReadableStatements(source,nextSource) {
  const parse=s=>acorn.parse(s,{ecmaVersion:'latest',sourceType:'module'});
  const a=parse(source),b=parse(nextSource),edits=[],counts={tdzGuard:0,emptyElse:0,invertedEmpty:0,switchBreak:0,switchLabel:0,switchValue:0};
  const comments=scan(source).comments;
  const clear=n=>n?.type==='BlockStatement'&&n.body.length===0&&!comments.some(c=>c.start>=n.start&&c.end<=n.end);
  const bare=n=>n?.type==='ExpressionStatement'&&n.expression.type==='Identifier';
  const assignment=n=>n?.type==='ExpressionStatement'&&n.expression.type==='AssignmentExpression'&&n.expression.left.type==='Identifier'?n.expression:undefined;
  const target=n=>assignment(n)?.left.name??(n?.type==='ExpressionStatement'&&n.expression.type==='UpdateExpression'&&n.expression.argument.type==='Identifier'?n.expression.argument.name:undefined);
  const text=n=>source.slice(n.start,n.end);
  const pending=[[a,b]];
  const equal=(x,y)=>JSON.stringify(clean(x))===JSON.stringify(clean(y));
  function clean(n){if(Array.isArray(n))return n.map(clean);if(!n||typeof n!=='object')return n;return Object.fromEntries(Object.entries(n).filter(([k])=>!['start','end','raw'].includes(k)).map(([k,v])=>[k,clean(v)]));}
  function negate(n) {
    if(n.type==='UnaryExpression'&&n.operator==='!')return text(n.argument);
    const complement={'==':'!=','!=':'==','===':'!==','!==':'==='};
    if(n.type==='BinaryExpression'&&complement[n.operator])return `((${text(n.left)}) ${complement[n.operator]} (${text(n.right)}))`;
    return `!(${text(n)})`;
  }
  while(pending.length) {
    const [x,y]=pending.pop();
    if(Array.isArray(x)&&Array.isArray(y)) {
      let i=0,j=0;
      while(i<x.length&&j<y.length){
        if(bare(x[i]) && target(x[i+1]) && target(y[j]) &&
            x[i].expression.name===target(x[i+1]) && equal(x[i+1],y[j])) {
          // The raw guarded-store certificate establishes RHS effects. This
          // lexer witness restricts the token change to that exact read only.
          assert(!comments.some(c=>c.start>=x[i].start&&c.end<=x[i].end));
          edits.push([x[i].start,x[i].end,'']);++counts.tdzGuard;++i;continue;
        }
        if(x[i]?.type==='VariableDeclaration'&&x[i].declarations.length===1 &&
            x[i].declarations[0].init && x[i].declarations[0].id.type==='Identifier' &&
            x[i+1]?.type==='SwitchStatement'&&y[j]?.type==='SwitchStatement' &&
            x[i+1].discriminant.type==='Identifier'&&x[i+1].discriminant.name===x[i].declarations[0].id.name &&
            equal(x[i].declarations[0].init,y[j].discriminant)) {
          const d=x[i].declarations[0];edits.push([x[i].start,x[i].end,''],[x[i+1].discriminant.start,x[i+1].discriminant.end,text(d.init)]);
          ++counts.switchValue;pending.push([x[i+1].cases,y[j].cases]);i+=2;++j;continue;
        }
        pending.push([x[i++],y[j++]]);
      }
      assert.equal(i,x.length,'unapproved removed statement');assert.equal(j,y.length,'unapproved added statement');continue;
    }
    if(x?.type==='IfStatement'&&y?.type==='IfStatement') {
      if(clear(x.alternate)&&!y.alternate) {
        const tokens=scan(source).tokens;
        const token=tokens.find(t=>t.start>=x.consequent.end&&t.end<=x.alternate.start&&t.type.label==='else');assert(token);
        edits.push([token.start,x.alternate.end,'']);++counts.emptyElse;
        pending.push([x.test,y.test],[x.consequent,y.consequent]);continue;
      }
      if(clear(x.consequent)&&x.alternate&&!y.alternate) {
        const inverted=negate(x.test);
        // Parse the witness condition before accepting the opposite arm.
        assert(equal(parse(`if (${inverted}) {}`).body[0].test,y.test),'wrong inverted predicate');
        edits.push([x.test.start,x.test.end,inverted]);
        if(x.alternate.type==='BlockStatement') {
          edits.push([x.consequent.start,x.alternate.start,'']);
          pending.push([x.alternate,y.consequent]);
        } else {
          assert(y.consequent.type==='BlockStatement'&&y.consequent.body.length===1,'missing braced arm');
          edits.push([x.consequent.start,x.alternate.start,'{ '],[x.end,x.end,' }']);
          pending.push([x.alternate,y.consequent.body[0]]);
        }
        ++counts.invertedEmpty;continue;
      }
    }
    if(x?.type==='LabeledStatement'&&x.body.type==='SwitchStatement'&&y?.type==='SwitchStatement') {
      const label=x.label.name;let uses=0;
      const work=[[x.body,x.body]];
      while(work.length){const [n,nearest]=work.pop();
        const owner=['SwitchStatement','WhileStatement','DoWhileStatement','ForStatement','ForOfStatement','ForInStatement'].includes(n.type)?n:nearest;
        if(n.type==='BreakStatement'&&n.label?.name===label){assert.equal(nearest,x.body,'switch is not innermost');edits.push([n.label.start,n.label.end,'']);++uses;}
        if(n.type==='ContinueStatement')assert.notEqual(n.label?.name,label);
        for(const v of Object.values(n))if(Array.isArray(v)){for(const c of v)if(c?.type)work.push([c,owner]);}else if(v?.type)work.push([v,owner]);
      }
      edits.push([x.start,x.body.start,'']);counts.switchBreak+=uses;++counts.switchLabel;
      const projected=structuredClone(x.body),queue=[projected];
      while(queue.length){const n=queue.pop();if(n.type==='BreakStatement'&&n.label?.name===label)n.label=null;
        for(const v of Object.values(n))if(Array.isArray(v)){for(const c of v)if(c?.type)queue.push(c);}else if(v?.type)queue.push(v);
      }
      pending.push([projected,y]);continue;
    }
    if(x===null||y===null||typeof x!=='object'||typeof y!=='object'){assert.deepEqual(x,y,'unapproved AST change');continue;}
    const keys=n=>Object.keys(n).filter(k=>!['start','end','raw'].includes(k)).sort();assert.deepEqual(keys(x),keys(y),'AST fields');
    for(const k of keys(x))pending.push([x[k],y[k]]);
  }
  let result=source;
  for(const [lo,hi,v]of edits.sort((p,q)=>q[0]-p[0]))result=result.slice(0,lo)+v+result.slice(hi);
  // All remaining changes must be grouping-only. Comments outside explicitly
  // empty blocks are conserved by the same real-lexer check.
  const normalized=s=>{
    const scanned=scan(s);
    return [...scanned.tokens.filter(t=>t.type.label!=='eof').map(t=>[t.start,t.end,false]),
      ...scanned.comments.map(c=>[c.start,c.end,true])].sort((a,b)=>a[0]-b[0])
      .map(([lo,hi,comment])=>s.slice(lo,hi)+(comment?'\n':' ')).join('');
  };
  // Statement deletion necessarily moves line boundaries. AST matching above
  // retains ASI semantics; compare real lexer spellings/comments independently
  // of indentation, with grouping-only differences in the remaining stream.
  const removed=inferGroupingRemovals(normalized(result),normalized(nextSource)).length;
  return {source:result,edits,counts,removed};
}
