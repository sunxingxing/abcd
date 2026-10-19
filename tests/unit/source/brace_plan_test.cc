#include "brace_plan.h"
#include "abcd/source/tree_printer.h"
#include "abcd/source/counting_sink.h"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <source_location>
using namespace abcd;using namespace abcd::source;
void Check(bool x,std::source_location at=std::source_location::current()){if(!x){std::cerr<<at.line()<<'\n';std::abort();}}
FunctionTree Tree(unsigned n=1,bool depth=false){
 FunctionTree t{};t.owner={0};t.callable=CallableKind::kFunction;t.strictness=Strictness::kStrict;
 t.parameter_scope={0};t.body_scope={1};t.body={0};
 t.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};t.origins={EntryOrigin{}};
 t.expressions={{TreeLiteral{IntegerLiteral{7}},{0}}};
 t.statements.push_back({BlockStmt{{1},0,n},{0}});
 if(depth){
  std::get<BlockStmt>(t.statements[0].statement).child_count=1;
  for(unsigned i=0;i<n;++i){t.scopes.push_back({ScopeKind::kBlock,ScopeId{i+1}});t.children.push_back({i+1});t.statements.push_back({BlockStmt{{i+2},i+1,1},{0}});}
  t.children.push_back({n+1});t.statements.push_back({ExpressionStmt{{0}},{0}});
 }else{
  for(unsigned i=0;i<n;++i)t.children.push_back({i+1});
  for(unsigned i=0;i<n;++i){t.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});t.statements.push_back({BlockStmt{{i+2},n+i,1},{0}});t.children.push_back({n+i+1});}
  for(unsigned i=0;i<n;++i)t.statements.push_back({ExpressionStmt{{0}},{0}});
 }
 return t;
}
struct Inputs {BindingInitializers b;NullishSyntax n;LoopSyntax l;explicit Inputs(std::size_t count){b.statements.resize(count);b.omitted.resize(count);b.statement_values.resize(count);b.declaration_completions.resize(count);b.declaration_values.resize(count);l.tail_starts.resize(count);l.initializer_owners.resize(count);}};
BracePlan Plan(const FunctionTree&t){Inputs x(t.statements.size());TreeCheckBudget b({});return BuildBracePlan(t,x.b,x.n,x.l,b);}
std::string Print(const FunctionTree&t){std::ostringstream out;auto p=PrintFunctionTree(t,out);Check(std::holds_alternative<PrintedFunction>(p));CountingSink sink;std::ostream count(&sink);Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(t,count)));Check(sink.Bytes()==out.str().size());
 const auto& positions=std::get<PrintedFunction>(p);
 for(const auto& span:positions.statements)Check(span.begin<=span.end&&span.end<=out.str().size());
 for(const auto anchor:positions.anchors)Check(anchor<=out.str().size());
 return out.str();}
FunctionTree Branch(){
 auto t=Tree();t.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}},
 {ScopeKind::kBlock,ScopeId{1}},{ScopeKind::kBlock,ScopeId{1}},
 {ScopeKind::kBlock,ScopeId{3}},{ScopeKind::kBlock,ScopeId{3}}};
 t.expressions={{TreeLiteral{BooleanLiteral{false}},{0}},{TreeLiteral{BooleanLiteral{true}},{0}},
 {TreeLiteral{IntegerLiteral{11}},{0}},{TreeLiteral{IntegerLiteral{22}},{0}},
 {TreeLiteral{IntegerLiteral{33}},{0}}};
 t.children={{1},{7},{4},{8},{9}};
 t.statements={{BlockStmt{{1},0,1},{0}},{IfStmt{{0},{2},StmtId{3}},{0}},
 {BlockStmt{{2},1,1},{0}},{BlockStmt{{3},2,1},{0}},{IfStmt{{1},{5},StmtId{6}},{0}},
 {BlockStmt{{4},3,1},{0}},{BlockStmt{{5},4,1},{0}},
 {ReturnStmt{ExprId{2}},{0}},{ReturnStmt{ExprId{3}},{0}},{ReturnStmt{ExprId{4}},{0}}};return t;
}
int main(int argc,char**){
 if(argc>1){std::cout<<Print(Branch());return 0;}
 {auto b=Branch();Check(Plan(b).statements[3]==BraceReason::kElseIf);
 Check(Plan(b).statements[2]==BraceReason::kRequired);Check(Plan(b).statements[5]==BraceReason::kRequired);
 Check(Print(b).find("else if")!=std::string::npos);}

 auto t=Tree();t.anchors={{{BlockBoundary{{1},0}},{0}},{{BlockBoundary{{1},1}},{0}}};t.operations={{{0},AnchorId{0}},{{1},AnchorId{1}}};Check(Plan(t).statements[1]==BraceReason::kStatementList);Check(Print(t).find("  {\n")==std::string::npos);
 {auto x=t;x.scopes[2].kind=ScopeKind::kCatch;Check(Plan(x).statements[1]==BraceReason::kScope);}
 {auto x=t;x.capture_scope=ScopeId{2};Check(Plan(x).statements[1]==BraceReason::kBinding);}
 {auto x=t;x.names={u"label"};std::get<BlockStmt>(x.statements[1].statement).label=LabelId{0};Check(Plan(x).statements[1]==BraceReason::kLabel);}
 {auto x=t;x.texts={u"use strict"};x.expressions[0].expression=TreeLiteral{TextId{0}};Check(Plan(x).statements[1]==BraceReason::kDirective);}
 {auto x=t;x.statements[2].statement=DeclarationStmt{{0},{}};Check(Plan(x).statements[1]==BraceReason::kContent);}
 {auto x=t;x.statements[2].statement=BreakStmt{{1},{}};Check(Plan(x).statements[1]==BraceReason::kLabel);}
 {auto x=t;x.statements[0].statement=LoopStmt{{1},{}};x.body={2};Check(Plan(x).statements[1]==BraceReason::kRequired);}
 for(auto kind:{DeclarationKind::kLet,DeclarationKind::kConst}){auto x=t;x.names={u"x"};x.symbols.push_back({{2},{0},BindingKind::kLocal,kind,NameEvidence::kSynthesized,{}});Check(Plan(x).statements[1]==BraceReason::kBinding);}
 {Inputs x(t.statements.size());x.b.statements[2]=BindingInitializer{{0},{0},DeclarationKind::kLet};
  TreeCheckBudget budget({});Check(BuildBracePlan(t,x.b,x.n,x.l,budget).statements[1]==BraceReason::kContent);}
 for(unsigned size:{200U,201U,400U,800U})for(bool depth:{false,true}){
  auto x=Tree(size,depth);Inputs inputs(x.statements.size());TreeCheckBudget budget({});auto plan=BuildBracePlan(x,inputs.b,inputs.n,inputs.l,budget);
  TreeCheckBudget exact({budget.Bytes(),budget.Work()});Check(BuildBracePlan(x,inputs.b,inputs.n,inputs.l,exact).statements==plan.statements);
  for(bool bytes:{false,true}){bool failed=false;try{TreeCheckBudget shortBudget({budget.Bytes()-(bytes?1:0),budget.Work()-(bytes?0:1)});(void)BuildBracePlan(x,inputs.b,inputs.n,inputs.l,shortBudget);}catch(TreeStructureError){failed=true;}Check(failed);}
  TreeCheckBudget verify({});Check(VerifyBracePlan(x,inputs.b,inputs.n,inputs.l,plan,verify));plan.statements[0]=BraceReason::kStatementList;TreeCheckBudget wrong({});Check(!VerifyBracePlan(x,inputs.b,inputs.n,inputs.l,plan,wrong));
 }
}
