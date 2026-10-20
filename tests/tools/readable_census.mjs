// Test-only grammar census; no engine input or output is rewritten.
import assert from 'node:assert/strict';
import {readFileSync,readdirSync,writeFileSync} from 'node:fs';
import {join,relative} from 'node:path';
import {createRequire} from 'node:module';
const ts=createRequire(import.meta.url)('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript/lib/typescript.js');
function count(source) {
 const scanner=ts.createScanner(ts.ScriptTarget.Latest,true,ts.LanguageVariant.Standard,source),aliases=[];let exporting=false,previous;
 for(let t=scanner.scan();t!==ts.SyntaxKind.EndOfFileToken;t=scanner.scan()) {
  if(t===ts.SyntaxKind.ExportKeyword)exporting=true;
  if(exporting&&previous===ts.SyntaxKind.AsKeyword&&t===ts.SyntaxKind.StringLiteral)aliases.push([scanner.getTokenPos(),scanner.getTextPos()]);
  if(t===ts.SyntaxKind.SemicolonToken)exporting=false;previous=t;
 }
 let text=source;for(const [a,b]of aliases.reverse())text=text.slice(0,a)+'x'.padEnd(b-a,' ')+text.slice(b);
 const file=ts.createSourceFile('input.ts',text,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
 assert.equal(file.parseDiagnostics.length,0,'census parse');
 const c={groupedAwait:0,groupedAwaitAssignment:0,groupedAwaitReturn:0,groupedAwaitStatement:0,requiredAwaitMember:0,groupedClassAssignment:0,groupedNullishFallback:0,groupedOptionalAdd:0,groupedOptional:0,requiredOptionalPostfix:0,groupedConditionalAssignment:0,requiredSequenceArgument:0,requiredAssignmentMember:0,negatedInequality:0,groupingPairs:0};
 const todo=[file];while(todo.length) {
  const n=todo.pop();ts.forEachChild(n,x=>{todo.push(x);});
  if(ts.isPrefixUnaryExpression(n)&&n.operator===ts.SyntaxKind.ExclamationToken) {
   let child=n.operand;while(ts.isParenthesizedExpression(child))child=child.expression;
   if(ts.isBinaryExpression(child)&&[ts.SyntaxKind.ExclamationEqualsToken,ts.SyntaxKind.ExclamationEqualsEqualsToken].includes(child.operatorToken.kind))++c.negatedInequality;
  }
  if(!ts.isParenthesizedExpression(n))continue;++c.groupingPairs;
  const p=n.parent,e=n.expression;
  const assignment=ts.isVariableDeclaration(p)&&p.initializer===n||ts.isBinaryExpression(p)&&p.operatorToken.kind===ts.SyntaxKind.EqualsToken&&p.right===n;
  const member=(ts.isPropertyAccessExpression(p)||ts.isElementAccessExpression(p))&&p.expression===n;
  if(ts.isAwaitExpression(e)) {
   ++c.groupedAwait;
   if(assignment)++c.groupedAwaitAssignment;
   if(ts.isReturnStatement(p))++c.groupedAwaitReturn;
   if(ts.isExpressionStatement(p))++c.groupedAwaitStatement;
   if(member)++c.requiredAwaitMember;
  }
  if(ts.isClassExpression(e)&&assignment)++c.groupedClassAssignment;
  if(ts.isBinaryExpression(e)&&e.operatorToken.kind===ts.SyntaxKind.QuestionQuestionToken)++c.groupedNullishFallback;
  if(e.questionDotToken||(e.flags&ts.NodeFlags.OptionalChain)){++c.groupedOptional;if((ts.isPropertyAccessExpression(p)||ts.isElementAccessExpression(p)||ts.isCallExpression(p))&&p.expression===n)++c.requiredOptionalPostfix;}
  if((e.questionDotToken||(e.flags&ts.NodeFlags.OptionalChain))&&ts.isBinaryExpression(p)&&p.operatorToken.kind===ts.SyntaxKind.PlusToken)++c.groupedOptionalAdd;
  if(ts.isConditionalExpression(e)&&assignment)++c.groupedConditionalAssignment;
  if(ts.isBinaryExpression(e)&&e.operatorToken.kind===ts.SyntaxKind.EqualsToken&&member)++c.requiredAssignmentMember;
  if(ts.isBinaryExpression(e)&&e.operatorToken.kind===ts.SyntaxKind.CommaToken&&(ts.isCallExpression(p)||ts.isNewExpression(p))&&p.arguments?.includes(n))++c.requiredSequenceArgument;
 }
 return c;
}
if(process.argv[2]==='--self-test') {
 const c=count('async function f(x){let a=(await x);return (await x);} x=(class C{}); f((x ?? y)); f((a,b)); (await x).m(); if (!(a !== b)) {}');
 assert.equal(c.groupedAwaitAssignment,1);assert.equal(c.groupedAwaitReturn,1);assert.equal(c.groupedClassAssignment,1);assert.equal(c.groupedNullishFallback,1);assert.equal(c.requiredSequenceArgument,1);assert.equal(c.requiredAwaitMember,1);assert.equal(c.negatedInequality,1);
 console.log('readable census: grammar contexts passed');
} else {
 const [root,out]=process.argv.slice(2),files=[],failures=[];const counts={};
 const dirs=[root];while(dirs.length) {
  const dir=dirs.pop();for(const d of readdirSync(dir,{withFileTypes:true})) {
   const path=join(dir,d.name);if(d.isDirectory()){dirs.push(path);continue;}
   if(d.name!=='manifest.json')continue;
   for(const r of JSON.parse(readFileSync(path)).records??[])if(r.source||r.incomplete_source) {
    const emitted=r.source||r.incomplete_source;
    try {const source=readFileSync(join(dir,emitted),'utf8'),row=count(source);files.push({file:relative(root,join(dir,emitted)),counts:row});for(const [k,v]of Object.entries(row))counts[k]=(counts[k]??0)+v;}
    catch(e){failures.push({file:emitted,error:String(e)});}
   }
  }
 }
 writeFileSync(out,JSON.stringify({files:files.length,counts,failures,records:files},null,2));console.log(JSON.stringify({files:files.length,counts,failures},null,2));
}
