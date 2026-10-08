import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {isDeepStrictEqual} from 'node:util';
import {parseJavaScript} from './class_reference.mjs';
const rows=execFileSync(process.argv[2],['--emit'],{encoding:'utf8',maxBuffer:32_000_000}).trim().split('\n').map(JSON.parse);
const failure=Symbol('throw'),token={},second={};
const tokenIdentity=Symbol('token'),secondIdentity=Symbol('second');
const identity=value=>value===token?tokenIdentity:value===second?secondIdentity:value;
function reference(row) {
  const {form,native}=row;
  const initialize=(name,value,constant=false)=>native||constant?`${constant?'const':'let'} ${name}=${value};`:`let ${name};${name}=${value};`;
  let body;
  if(form===8)body='let current=current;let scratch;';
  else {
    let value=form===0?'42':(form===20||form===27)?'seed(current)':(form===25||form===28)?'seed()':([3,4,5,7,12].includes(form)||(form>=17&&form<22))?'seed()':form===11?'{}':(form===2||form===26)?'(report(input),input)':'input';
    const primitiveValues={
      29:'flag?(report(input),42):(seed(input),42)',
      30:'flag?(report(input),42):(seed(input),43)',
      31:'input=(report(input),42)',
      32:'flag?(report(input),-0):(seed(input),-0)',
      33:'flag?(report(input),-0):(seed(input),0)',
      34:'(report(input),0)||(seed(input),42)',
      35:'(report(input),0)&&(seed(input),42)',
      36:'(report(input),null)??(seed(input),42)',
      37:'(report(input),0)??(seed(input),42)',
      38:'flag?(report(input),"text"):(seed(input),"text")',
      39:'flag?(report(input),"text"):(seed(input),"other")',
      40:'seed()?(report(input),42):(seed(input),42)',
      41:'(current,42)',
      42:'(input=(report(input),42),"text")',
      43:'flag?(report(input),NaN):(seed(input),NaN)',
      44:'flag?(report(input),true):(seed(input),true)',
      45:'flag?(report(input),void 0):(seed(input),void 0)',
      46:'(report(input),"")||(seed(input),42)',
      47:'(report(input),"text")&&(seed(input),42)',
      48:'42',
      49:'seed.value=(report(input),42)',
      50:'(report(input),NaN)||(seed(input),42)',
      51:'(report(input),NaN)&&(seed(input),42)',
      52:'(report(input),false)?(report(input),42):(seed(input),43)',
      53:'(report(input),-1)||(seed(input),42)',
      54:'(report(input),Infinity)&&(seed(input),42)',
      55:'(report(input),void 0)??(seed(input),42)',
      56:'(report(input),-0)?(report(input),42):(seed(input),43)',
      57:'(report(input),true)?(seed(input),42):seed()',
      58:'(report(input),false)?(seed(input),42):seed()',
      59:'(report(input),42)',
      60:'input',61:'input',62:'input',63:'input',64:'input',65:'input',66:'true',67:'false',
      68:'42',69:'42',70:'42',71:'42',72:'42',73:'0'
    };
    if(form>=29)value=primitiveValues[form];
    body=(form===59?'let current;try{current=(report(input),42);}catch(error){report(current);}':initialize('current',value))+
      (form===7?'report(scratch);':form===15?'scratch=seed();':'')+
      (form===68?'if(flag){current=42;}else{current=42;}':form===69?'if(flag){current=42;}else{current=43;}':
       form===70?'while(flag()){current=current;}':form===71?'while(flag()){current=43;}':'')+
      ([3,4,11,12].includes(form)?'let scratch;':initialize('scratch',({
        60:'(report(input),true)?current:seed()',61:'(report(input),0)||current',
        62:'(report(input),42)&&current',63:'(report(input),null)??current',
        64:'(report(input),false)&&current',65:'(report(input),false)?current:seed()',
        66:'(report(input),current)?input:seed()',67:'(report(input),current)?input:seed()',73:'++current'
      })[form]??((form===22||form===23)?'seed()':form===16?'(report(input),42)':form===48?'(report(input),current)':'current'),form===6));
  }
  if(form>=74) {
    const value=form===75?'(report(input),current)??input':form===76?'(report(input),current)?seed():input':form===77?'(report(input),current)':'current';
    body=(form===79?'report(current);':'')+'let current;'+(form===80?'if(flag){current=input;}':'')+initialize('scratch',value);
    if(form===78)body+='while(flag()){current=scratch;scratch=current;}';
  }
  if(form===72)body+='while(flag()){current=scratch;scratch=current;}';
  if(form===5||form===15)body+='++scratch;current=scratch;';
  if(form===13)body+='if(flag){++scratch;current=scratch;}';
  if(form===14)body+='while(flag()){++scratch;current=scratch;}';
  if(form===18)body+='scratch=seed();';
  if(form===19)body+='current=seed();';
  if(form===21)body+='while(flag()){scratch=seed();}';
  if(form===22)body+='scratch=seed();report(scratch);current=scratch;';
  if(form===23)body+='try{scratch=seed();report(scratch);current=scratch;}catch(error){report(current);}';
  if(form>=24&&form<29)body+='current=seed();';
  if(form===16)body+='report(scratch);current=scratch;';
  if(form===10)body+='try{scratch=seed();current=scratch;}catch(error){report(current);}';
  const result=form===17||form===21?'input':form===18?'scratch':[3,4,5,8,10,11,12,13,14,15,16,19,20,22,23,24,25,26,27,28].includes(form)?'current':'scratch';
  const tail=([3,17,20,21,28].includes(form)?'':`report(${result});`)+`return ${result};`;
  body+=form===9?`{let input=17;${tail}}`:tail;
  return Function(`return function(input,seed,report,flag){'use strict';${body}}`)();
}
function observe(fn,row,input,fault,throwSeed,coercion) {
  const trace=[];let at=0,returned,thrown,object,iterations=0;
  const event=(kind,value)=>{trace.push([kind,identity(value)]);if(++at===fault)throw failure;};
  const numeric={ [Symbol.toPrimitive](hint){event('numeric',hint);if(coercion)throw failure;return 3;} };
  const seed=function(){assert.equal(this,undefined);event('seed',input);if(throwSeed)throw failure;return (row.form>=22&&row.form<29)||row.form===65||row.form===67?second:row.form===5?numeric:input;};
  const report=function(value){assert.equal(this,undefined);
    if(row.form===11){object??=value;assert.equal(value,object);value.visits=(value.visits??0)+1;event('report',value.visits);}
    else event('report',value);
  };
  if(row.form===49)Object.defineProperty(seed,'value',{set(value){assert.equal(this,seed);event('setter',value);}});
  const flag=(row.form===14||row.form===21||((row.form>=70&&row.form<=72)||row.form===78))?()=>{event('iteration',iterations);return iterations++<(row.form>=70&&coercion?0:2);}:!coercion;
  try{ returned=fn(input,seed,report,flag);if(row.form===11){assert.equal(returned,object);returned=token;} }
  catch(error){thrown=error;}
  if(thrown instanceof ReferenceError)thrown='ReferenceError';
  return {trace,returned:identity(returned),thrown};
}
let comparisons=0,mutations=0;
const observed=(source,row,...args)=>observe(Function(`return ${source}`)(),row,...args);
for(const row of rows) {
  parseJavaScript(row.source);
  const expected=reference(row),actual=Function(`return ${row.source}`)();
  for(const input of [undefined,null,false,0,-0,NaN,42,'text',token,second])
    for(const fault of [0,1,2,3,4])for(const throwSeed of [false,true])for(const coercion of [false,true]) {
      assert.deepEqual(observe(actual,row,input,fault,throwSeed,coercion),observe(expected,row,input,fault,throwSeed,coercion),JSON.stringify(row));++comparisons;
    }
  if(row.form<=3||row.form===16||row.form===17||row.form===21||row.form===28||
      (row.form>=29&&row.form<58&&![30,33,39,41].includes(row.form))||
      (row.form>=60&&row.form<68&&![65,67].includes(row.form))||[68,70,72,74,75,76,77,78].includes(row.form))assert.doesNotMatch(row.source,/\b(?:current|scratch)\b/);
  if(row.form===5) {
    const ast=parseJavaScript(row.source);const declarations=[];
    const walk=node=>{if(!node||typeof node!=='object')return;if(node.type==='VariableDeclaration')declarations.push(...node.declarations);for(const value of Object.values(node))if(Array.isArray(value))value.forEach(walk);else if(value&&typeof value==='object')walk(value);};
    walk(ast);assert.equal(declarations.length,1,'all successful writes share one live slot');
  }
  if(row.count===0&&!row.reverse&&row.native) {
    if(row.form===2) {
      assert.notDeepEqual(observed('(function(input){return input;})',row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form===5) {
      assert.notDeepEqual(observed('(function(input,seed,report){let value=seed();report(value);return value;})',row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form===7||row.form===8||row.form===15||row.form===20) {
      assert.notDeepEqual(observed('(function(input){return input;})',row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form===16) {
      assert.notDeepEqual(observed('(function(input,seed,report){report(input);let value=void 0;report(value);report(value);return value;})',row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form===17) {
      assert.notDeepEqual(observed('(function(input){return input;})',row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form===18) {
      assert.notDeepEqual(observed('(function(input,seed,report){let scratch=current;scratch=seed();report(scratch);return scratch;})',row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form===19) {
      assert.notDeepEqual(observed('(function(input,seed,report){let current=seed();report(current);return current;})',row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form===21) {
      for(const source of ['(function(input,seed,report,flag){while(flag()){seed();}return input;})',
          '(function(input,seed,report,flag){seed();while(flag()){}return input;})']) {
        assert.notDeepEqual(observed(source,row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
      }
    }
    if(row.form===22) {
      assert.notDeepEqual(observed('(function(input,seed,report){let current=input;current=seed();report(current);report(current);return current;})',row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form===23) {
      assert.notDeepEqual(observed('(function(input,seed,report){let current=seed();try{current=seed();report(current);}catch(e){report(current);}report(current);return current;})',row,token,2,false,false),observe(expected,row,token,2,false,false));++mutations;
    }
    if(row.form===25) {
      assert.notDeepEqual(observed('(function(input,seed,report){let current=seed();report(current);return current;})',row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form===26) {
      assert.notDeepEqual(observed('(function(input,seed,report){let current=seed();report(current);return current;})',row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form===27) {
      assert.notDeepEqual(observed('(function(input,seed,report){let current;seed(current);current=seed();report(current);return current;})',row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form===28) {
      assert.notDeepEqual(observed('(function(input,seed){return seed();})',row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form>=29&&row.form<68&&row.form!==41&&row.form!==59) {
      // Removing the original initializer also removes its condition/calls/
      // parameter write or TDZ read. Independent execution rejects that edit.
      const ast=parseJavaScript(row.source),body=ast.body[0].expression.body.body;
      if(body.length>1) {
        const wrong=row.source.slice(0,body[0].start)+row.source.slice(body[0].end);
        const differs=[false,true].some(coercion=>!isDeepStrictEqual(observed(wrong,row,token,0,false,coercion),observe(expected,row,token,0,false,coercion)));
        assert.ok(differs,JSON.stringify(row));++mutations;
      }
    }
    if(row.form===41) {
      assert.notDeepEqual(observed('(function(){return 42;})',row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form===59) {
      assert.notDeepEqual(observed('(function(input,seed,report){let value=42;try{report(input);}catch(error){report(value);}report(value);return value;})',row,token,1,false,false),observe(expected,row,token,1,false,false));++mutations;
    }
    if(row.form>=68) {
      const wrong=row.form===73?'(function(){return 0;})':row.form===69||row.form===71?'(function(){return 42;})':'(function(){return 43;})';
      assert.notDeepEqual(observed(wrong,row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form===10) {
      assert.notDeepEqual(observed('(function(input,seed,report){let value;try{value=seed();}catch(e){report(value);}report(value);return value;})',row,token,0,true,false),observe(expected,row,token,0,true,false));++mutations;
    }
  }
}
console.log(JSON.stringify({sources:rows.length,comparisons,mutations}));
