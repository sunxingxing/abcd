#include "readable_statements.h"
#include "original_flow.h"
#include "abcd/source/tree_printer.h"
#include "abcd/source/counting_sink.h"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <source_location>
using namespace abcd;using namespace abcd::source;
void Check(bool x,std::source_location at=std::source_location::current()){if(!x){std::cerr<<at.line()<<'\n';std::abort();}}
FunctionTree Branch(unsigned count=1){
 FunctionTree t{};t.owner={0};t.callable=CallableKind::kFunction;t.strictness=Strictness::kStrict;
 t.parameter_scope={0};t.body_scope={1};t.body={0};t.origins={EntryOrigin{}};
 t.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
 t.statements.push_back({BlockStmt{{1},0,count},{0}});
 for(unsigned i=0;i<count;++i)t.children.push_back({1+3*i});
 for(unsigned i=0;i<count;++i){
  const auto scope=unsigned(t.scopes.size());t.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});t.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
  t.expressions.push_back({TreeLiteral{BooleanLiteral{false}},{0}});
  t.statements.push_back({IfStmt{{i},{2+3*i},StmtId{3+3*i}},{0}});
  t.statements.push_back({BlockStmt{{scope},count,0},{0}});t.statements.push_back({BlockStmt{{scope+1},count,0},{0}});
 }return t;
}
struct Inputs {
 BindingInitializers b;NullishSyntax n;LoopSyntax l;
 explicit Inputs(const FunctionTree&t){const auto count=t.statements.size();b.statements.resize(count);b.omitted.resize(count);b.statement_values.resize(count);b.declaration_completions.resize(count);b.declaration_values.resize(count);b.replacements.resize(t.expressions.size());b.evaluations.resize(t.expressions.size(),ExpressionEvaluation::kInert);l.tail_starts.resize(count);l.initializer_owners.resize(count);}
};
ReadableStatements Plan(const FunctionTree&t,const OriginalFlow* original=nullptr){Inputs in(t);TreeCheckBudget b({});return BuildReadableStatements(t,in.b,in.n,in.l,original,b);}
std::string Print(const FunctionTree&t){std::ostringstream out;auto p=PrintFunctionTree(t,out);if(const auto* e=std::get_if<TreePrintFailure>(&p))std::cerr<<"print "<<unsigned(e->kind)<<" cause "<<e->cause.index()<<" statements "<<t.statements.size()<<"\n";Check(std::holds_alternative<PrintedFunction>(p));CountingSink sink;std::ostream count(&sink);Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(t,count)));Check(sink.Bytes()==out.str().size());for(const auto&span:std::get<PrintedFunction>(p).statements)Check(span.begin<=span.end&&span.end<=out.str().size());return out.str();}
FunctionTree Guard(){auto t=Branch();t.children={{1},{2}};t.scopes.resize(2);t.names={u"x"};t.symbols={{{1},{0},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kSynthesized,{}}};t.expressions={{SymbolExpr{{0}},{0}},{TreeLiteral{IntegerLiteral{4}},{0}},{AssignmentExpr{SymbolTarget{{0}},{1}},{0}}};t.statements={{BlockStmt{{1},0,2},{0}},{ExpressionStmt{{0}},{0}},{ExpressionStmt{{2}},{0}}};return t;}
OriginalFlow Certificate(){OriginalFlow o;o.operations.resize(2);for(auto& x:o.operations)x.life=analysis::ValueLife::kLive;o.action_region_members={OriginalActionRegionId{0},OriginalActionRegionId{0}};o.action_regions.push_back({{0},{0},{1},{0},{0},{},OriginalReadKind::kCapture});return o;}
FunctionTree Switch(){
 auto t=Branch();t.children={{1},{2},{4},{6}};
 t.names={u"cache"};t.symbols={{{1},{0},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,{}}};
 t.scopes.push_back({ScopeKind::kBlock,ScopeId{2}});
 t.expressions={{BinaryExpr{analysis::BinaryOperator::kStrictEqual,{1},{2}},{0}},{SymbolExpr{{0}},{0}},{TreeLiteral{IntegerLiteral{1}},{0}},{TreeLiteral{IntegerLiteral{7}},{0}}};
 t.statements={{BlockStmt{{1},0,2},{0}},{DeclarationStmt{{0},ExprId{3}},{0}},
 {SwitchStmt{{3},LabelId{0}},{0}},{BlockStmt{{2},2,1},{0}},
 {SwitchCaseStmt{ExprId{0},{5}},{0}},{BlockStmt{{3},3,1},{0}},{BreakStmt{{2},LabelId{0}},{0}}};
 t.labels={{2}};return t;
}
int main(int argc,char**){
 auto branch=Branch();Check(Plan(branch).branches[1]==ReadableBranch::kDropElse);
 if(argc>1){std::cout<<Print(branch);return 0;}
 Check(Print(branch).find("else")==std::string::npos);
 {auto t=branch;t.expressions.push_back({TreeLiteral{IntegerLiteral{7}},{0}});t.statements.push_back({ReturnStmt{ExprId{1}},{0}});t.children.push_back({4});std::get<BlockStmt>(t.statements[3].statement).child_count=1;Check(Plan(t).branches[1]==ReadableBranch::kInvert);Check(Print(t).find("if (!false)")!=std::string::npos);}
 {auto t=branch;t.names={u"lex"};t.symbols={{{3},{0},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kSynthesized,{}}};Check(Plan(t).branches[1]==ReadableBranch::kInvert);t.symbols[0].scope={2};Check(Plan(t).branches[1]==ReadableBranch::kDropElse);t.symbols.push_back({{3},{0},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kSynthesized,{}});Check(Plan(t).branches[1]==ReadableBranch::kKeep);}
 {auto t=branch;t.capture_scope=ScopeId{3};Check(Plan(t).branches[1]==ReadableBranch::kInvert);std::get<BlockStmt>(t.statements[2].statement).label=LabelId{0};Check(Plan(t).branches[1]==ReadableBranch::kKeep);}
 {auto sw=Switch();Check(Plan(sw).plain_breaks[6]);Check(Plan(sw).omit_labels[2]);
  Check(Plan(sw).switch_values[2]==ExprId{3});Check(Plan(sw).omit_snapshots[1]);
  {auto escaped=sw;escaped.closure_captures={{0}};Check(!Plan(escaped).switch_values[2]);}
  {auto observed=sw;observed.expressions.push_back({SymbolExpr{{0}},{0}});Check(!Plan(observed).switch_values[2]);}
  sw.statements[5].statement=LoopStmt{{7},{}};sw.statements.push_back({BlockStmt{{4},3,1},{0}});
  Check(!Plan(sw).plain_breaks[6]);Check(!Plan(sw).omit_labels[2]);}
 auto t=Guard();auto o=Certificate();Check(Plan(t,&o).guards[1]==ReadableGuard::kRemove);Check(Plan(t).guards[1]==ReadableGuard::kNoCertificate);
 {auto x=t;std::get<SymbolExpr>(x.expressions[0].expression).loading=analysis::ModuleLoading::kLazy;Check(Plan(x,&o).guards[1]==ReadableGuard::kNoCertificate);}
 {auto x=t;x.expressions[2].expression=UpdateExpr{{0},UpdateDirection::kIncrement,UpdateResult::kPostfix};Check(Plan(x,&o).guards[1]==ReadableGuard::kRemove);}
 {auto wrong=o;wrong.action_regions[0].reference={9};Check(Plan(t,&wrong).guards[1]==ReadableGuard::kNoCertificate);}
 {auto wrong=o;wrong.action_region_members[1].reset();Check(Plan(t,&wrong).guards[1]==ReadableGuard::kNoCertificate);}
 {auto x=t;std::get<SymbolTarget>(std::get<AssignmentExpr>(x.expressions[2].expression).target).symbol={1};x.symbols.push_back(x.symbols[0]);Check(Plan(x,&o).guards[1]==ReadableGuard::kBinding);}
 {Inputs in(t);in.b.evaluations[1]=ExpressionEvaluation::kRequired;TreeCheckBudget b({});Check(BuildReadableStatements(t,in.b,in.n,in.l,&o,b).guards[1]==ReadableGuard::kRhsEffect);}
 {Inputs in(t);in.b.literals.resize(1);in.b.literals[0]=PrimitiveId{0};TreeCheckBudget b({});Check(BuildReadableStatements(t,in.b,in.n,in.l,&o,b).guards[1]==ReadableGuard::kProjection);}
 {Inputs in(t);in.b.statements[2]=BindingInitializer{{0},{2},DeclarationKind::kLet};TreeCheckBudget b({});Check(BuildReadableStatements(t,in.b,in.n,in.l,&o,b).guards[1]==ReadableGuard::kProjection);}
 for(unsigned n:{200U,201U,400U,800U}){
  auto tree=Branch(n);Inputs in(tree);TreeCheckBudget budget({});auto p=BuildReadableStatements(tree,in.b,in.n,in.l,nullptr,budget);
  TreeCheckBudget exact({budget.Bytes(),budget.Work()});Check(BuildReadableStatements(tree,in.b,in.n,in.l,nullptr,exact)==p);
  for(bool bytes:{false,true}){bool failed=false;try{TreeCheckBudget small({budget.Bytes()-(bytes?1:0),budget.Work()-(bytes?0:1)});(void)BuildReadableStatements(tree,in.b,in.n,in.l,nullptr,small);}catch(TreeStructureError){failed=true;}Check(failed);}
  TreeCheckBudget verify({});Check(VerifyReadableStatements(tree,in.b,in.n,in.l,nullptr,p,verify));p.branches[1]=ReadableBranch::kKeep;TreeCheckBudget bad({});Check(!VerifyReadableStatements(tree,in.b,in.n,in.l,nullptr,p,bad));
  std::cout<<n<<" bytes="<<budget.Bytes()<<" work="<<budget.Work()<<'\n';
 }
}
