// Test-only syntax census. TS's parser covers both JS and emitted ArkTS fields.
// Reasons describe printed grammar, not recovery support or latent IR identity.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync,readdirSync} from 'node:fs';
import {resolve,relative,join} from 'node:path';
import {createHash} from 'node:crypto';
const parser='/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript/lib/typescript.js';
const ts=createRequire(import.meta.url)(parser),K=ts.SyntaxKind;
const canonicalTokens=['EqualsToken','CommaToken','LessThanToken','GreaterThanToken','EqualsEqualsToken','EqualsEqualsEqualsToken','ExclamationEqualsToken','ExclamationEqualsEqualsToken','PlusToken','MinusToken','AsteriskToken','SlashToken','AsteriskAsteriskToken','BarBarToken','AmpersandAmpersandToken','QuestionQuestionToken'];
const tokenName=k=>canonicalTokens.find(name=>K[name]===k)||K[k];
const [root,out]=process.argv.slice(2),rows=[],failures=[];
function context(p,n){
 if(ts.isCallExpression(p)||ts.isNewExpression(p))return p.expression===n?(ts.isNewExpression(p)?'Constructor':'CallCallee'):'AssignmentValue/'+(ts.isNewExpression(p)?'NewArgument':'CallArgument');
 if(ts.isPropertyAccessExpression(p)||ts.isElementAccessExpression(p))return p.expression===n?'MemberBase':'Expression/ComputedKey';
 if(ts.isBinaryExpression(p)){const op=p.operatorToken.kind;return op===K.EqualsToken&&p.right===n?'AssignmentValue/Assignment':'Binary/'+tokenName(op)+'/'+(p.left===n?'left':'right');}
 if(ts.isReturnStatement(p)||ts.isThrowStatement(p))return 'Expression/'+K[p.kind];
 if(ts.isVariableDeclaration(p)||ts.isParameter(p)||ts.isPropertyAssignment(p)||ts.isArrayLiteralExpression(p)||ts.isSpreadElement(p))return 'AssignmentValue/'+K[p.kind];
 if(ts.isConditionalExpression(p))return p.condition===n?'ConditionalTest':'AssignmentValue/ConditionalArm';
 if(ts.isArrowFunction(p)&&p.body===n)return 'ArrowBody';
 if(ts.isExpressionStatement(p))return 'StatementStart';
 if(ts.isIfStatement(p)||ts.isWhileStatement(p)||ts.isDoStatement(p)||ts.isSwitchStatement(p))return 'Expression/'+K[p.kind];
 if(ts.isParenthesizedExpression(p))return 'Expression/Grouping';
 if(ts.isForOfStatement(p)&&p.expression===n)return 'AssignmentValue/ForOf';
 if(ts.isForInStatement(p)&&p.expression===n)return 'Expression/ForIn';
 if(ts.isPrefixUnaryExpression(p)||ts.isTypeOfExpression(p)||ts.isVoidExpression(p)||ts.isAwaitExpression(p))return 'Unary/'+K[p.kind];
 return 'Unknown/'+K[p.kind];
}
function reason(c,e){
 if(ts.isBinaryExpression(e)&&e.operatorToken.kind===K.CommaToken)return ['kPrecedence','sequence-or-value-reference'];
 if(c==='StatementStart'&&(ts.isObjectLiteralExpression(e)||ts.isFunctionExpression(e)||ts.isClassExpression(e)))return ['kStatementStart','leading-declaration-hazard'];
 if(c==='StatementStart'&&ts.isStringLiteral(e))return ['kDirective','directive-protection'];
 if(c==='ArrowBody'&&ts.isObjectLiteralExpression(e))return ['kArrowBody','concise-object'];
 if(ts.isVoidExpression(e))return ['kUnknown','literal-envelope/void'];
 if(ts.isPrefixUnaryExpression(e)&&ts.isNumericLiteral(e.operand))return ['kUnknown','literal-envelope/signed-number'];
 if(ts.isObjectLiteralExpression(e)&&c==='MemberBase')return ['kUnknown','object-member-or-name-envelope'];
 if(c==='StatementStart' && ts.isAwaitExpression(e))return ['kNone','legacy-await-envelope'];
 if(c.startsWith('Unknown'))return ['kUnknown','unmapped-context'];
 if(c.startsWith('AssignmentValue')||c.startsWith('Expression/'))return ['kNone','grammar-allows-assignment-expression'];
 return ['kPrecedence','operator-or-postfix-boundary'];
}
if(root==='--self-test') {
 const f=ts.createSourceFile('census.js','new C((void 0)); a = ({["x"]: () => 1})["x"]; if ((a = b).x) {}',ts.ScriptTarget.Latest,true,ts.ScriptKind.JS);
 const work=[f],seen=[];while(work.length){const n=work.pop();ts.forEachChild(n,x=>{work.push(x);});if(ts.isParenthesizedExpression(n))seen.push([context(n.parent,n),K[n.expression.kind]]);}
 assert.deepEqual(seen.sort(),[['AssignmentValue/NewArgument','VoidExpression'],['MemberBase','BinaryExpression'],['MemberBase','ObjectLiteralExpression']].sort());
 console.log('grouping census: grouping-only occurrences and parent contexts passed');process.exit(0);
}
const stack=[resolve(root)];while(stack.length){const dir=stack.pop();for(const d of readdirSync(dir,{withFileTypes:true})){
 const path=join(dir,d.name);if(d.isDirectory()){stack.push(path);continue;}if(!/\.(mjs|js|ets)$/.test(d.name))continue;
 const source=readFileSync(path,'utf8');
 // The pinned TS parser predates string-named export aliases. Substitute only
 // those lexer tokens with same-width identifiers for parsing; original offsets
 // and literal spellings in the census stay intact. No output is rewritten.
 const scanner=ts.createScanner(ts.ScriptTarget.Latest,true,ts.LanguageVariant.Standard,source),edits=[];let exporting=false,previous;
 for(let token=scanner.scan();token!==K.EndOfFileToken;token=scanner.scan()){if(token===K.ExportKeyword)exporting=true;if(exporting&&previous===K.AsKeyword&&token===K.StringLiteral)edits.push([scanner.getTokenPos(),scanner.getTextPos()]);if(token===K.SemicolonToken)exporting=false;previous=token;}
 let parsed=source;for(const [start,end]of edits.reverse())parsed=parsed.slice(0,start)+'x'.padEnd(end-start,' ')+parsed.slice(end);
 const file=ts.createSourceFile(path,parsed,ts.ScriptTarget.Latest,true,d.name.endsWith('.ets')?ts.ScriptKind.TS:ts.ScriptKind.JS);
 if(file.parseDiagnostics.length){failures.push({file:relative(root,path),diagnostics:file.parseDiagnostics.map(x=>x.messageText)});continue;}
 const todo=[file];while(todo.length){const n=todo.pop();ts.forEachChild(n,x=>{todo.push(x);});if(!ts.isParenthesizedExpression(n))continue;
 const c=context(n.parent,n),[r,detail]=reason(c,n.expression),start=n.getStart(file),end=n.end;
 let head=n;while(head.parent && ((ts.isPropertyAccessExpression(head.parent)||ts.isElementAccessExpression(head.parent)||ts.isCallExpression(head.parent))&&head.parent.expression===head || ts.isBinaryExpression(head.parent)&&head.parent.left===head))head=head.parent;
 rows.push({operator:n.expression.operatorToken?tokenName(n.expression.operatorToken.kind):'',statement_start:ts.isExpressionStatement(head.parent),file:relative(root,path),start,end,parent:c,child:K[n.expression.kind],reason:r,detail,example:source.slice(start,end).replace(/\s+/g,' ').slice(0,160)});
 }}}
const probe=spawnSync(resolve('build/regression/grouping_census_probe'),[],{input:rows.map(x=>JSON.stringify(x)).join('\n')+'\n',encoding:'utf8',maxBuffer:16*1024*1024});
if(probe.status!==0)throw Error(probe.stderr||'grouping census probe failed');
const decisions=probe.stdout.trim().split('\n');assert.equal(decisions.length,rows.length);
rows.forEach((x,i)=>{x.reason=decisions[i];});
const groups=new Map();for(const x of rows){const key=[x.parent,x.child,x.reason,x.detail].join('|');const g=groups.get(key)||{parent:x.parent,child:x.child,reason:x.reason,detail:x.detail,count:0,example:x.example,file:x.file};g.count++;groups.set(key,g);}
const classes=[...groups.values()].sort((a,b)=>b.count-a.count);
writeFileSync(out,JSON.stringify({parser,parser_sha256:createHash('sha256').update(readFileSync(parser)).digest('hex'),total:rows.length,failures,classes,occurrences:rows},null,2));
console.log(JSON.stringify({total:rows.length,failures:failures.length,top15:classes.slice(0,15)},null,2));
