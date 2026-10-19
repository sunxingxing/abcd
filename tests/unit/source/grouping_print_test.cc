#include "abcd/source/tree_printer.h"
#include "abcd/source/counting_sink.h"
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <source_location>
using namespace abcd;using namespace abcd::source;
namespace {
void Check(bool x,std::source_location at=std::source_location::current()){if(!x){std::cerr<<at.line()<<"\n";std::abort();}}
FunctionTree Make(std::string_view form,unsigned count=1){
 FunctionTree t{};t.owner={0};t.callable=CallableKind::kFunction;t.strictness=Strictness::kStrict;
 t.parameter_scope={0};t.body_scope={1};t.body={0};
 t.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};t.origins={EntryOrigin{}};
 t.names={u"f",u"flag",u"x",u"p",u"push"};
 for(unsigned i=0;i<3;++i){t.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kSynthesized,{}});t.parameters.push_back({{i},{},false});}
 auto add=[&](TreeExpression e){ExprId id{static_cast<std::uint32_t>(t.expressions.size())};t.expressions.push_back({std::move(e),{0}});return id;};
 auto read=[&](unsigned i){return add(SymbolExpr{{i}});};
 auto callee=read(0);auto reference=CallReference::kValue;
 if(form=="member"||form=="base"){
   auto base=callee;
   if(form=="base") {t.expressions.pop_back();base=add(AssignmentExpr{SymbolTarget{{2}},add(EmptyArrayExpr{})});}
   callee=add(MemberExpr{base,NameId{4}});reference=CallReference::kMember;
 }
 for(unsigned i=0;i<count;++i){
   if(form=="void"){t.arguments.push_back(add(TreeLiteral{UndefinedLiteral{}}));continue;}
   if(form=="evalalias"){t.texts.push_back(u"typeof x");t.arguments.push_back(add(TreeLiteral{TextId{0}}));continue;}
   if(form=="rebind"){t.arguments.push_back(add(AssignmentExpr{SymbolTarget{{0}},read(1)}));continue;}
   auto update=add(UpdateExpr{{2},UpdateDirection::kIncrement,
       form=="sign" ? UpdateResult::kPrefix : UpdateResult::kPostfix});
   if(form=="sign") update=add(UnaryExpr{TreeUnary::kPlus,update});
   if(form=="updatebase") update=add(MemberExpr{update,NameId{3}});
   const auto property=static_cast<std::uint32_t>(t.properties.size());t.properties.push_back({{3},update,{0}});
   const auto object=add(ObjectExpr{{property,1}});
   const auto plus=add(BinaryExpr{analysis::BinaryOperator::kAdd,read(2),add(TreeLiteral{IntegerLiteral{1}})});
   const auto assignment=add(AssignmentExpr{SymbolTarget{{2}},plus});
   const auto sequence=add(SequenceExpr{assignment,add(EmptyObjectExpr{})});
   const auto chosen=add(ConditionalExpr{read(1),object,sequence});
   t.arguments.push_back(chosen);
 }
 const auto result=add(CallExpr{callee,{0,count},reference});
 t.statements={{BlockStmt{{1},0,1},{0}},{ReturnStmt{result},{0}}};t.children={{1}};return t;
}
FunctionTree Head(std::string_view form) {
 auto t=Make("value");
 t.expressions.clear();t.properties.clear();t.arguments.clear();
 auto add=[&](TreeExpression e){ExprId id{static_cast<std::uint32_t>(t.expressions.size())};t.expressions.push_back({std::move(e),{0}});return id;};
 if(form=="voidconstruct") {
   const auto constructor=add(SymbolExpr{{0}});
   t.arguments={add(TreeLiteral{UndefinedLiteral{}}),add(EmptyObjectExpr{})};
   t.statements[1].statement=ReturnStmt{add(ConstructExpr{constructor,ArgumentRange{0,2}})};return t;
 }
 if(form=="lethead") {
   t.strictness=Strictness::kSloppy;t.names[2]=u"let";
   const auto value=add(SymbolExpr{{2}});
   const auto key=add(TreeLiteral{IntegerLiteral{0}});
   const auto member=add(MemberExpr{value,ComputedKey{key}});
   t.statements[1].statement=ExpressionStmt{member};
   t.statements.push_back({ReturnStmt{add(SymbolExpr{{2}})},{0}});t.children.push_back({2});
   std::get<BlockStmt>(t.statements[0].statement).child_count=2;return t;
 }
 const auto value=add(SymbolExpr{{2}});
 t.properties.push_back({{3},value,{0}});
 const auto object=add(ObjectExpr{{0,1}});
 auto result=add(MemberExpr{object,NameId{3}});
 if(form=="objectstatement") {
   t.statements[1].statement=ExpressionStmt{result};
   t.statements.push_back({ReturnStmt{add(SymbolExpr{{2}})},{0}});t.children.push_back({2});
   std::get<BlockStmt>(t.statements[0].statement).child_count=2;
 }else t.statements[1].statement=ReturnStmt{result};
 return t;
}
std::string Print(const FunctionTree& t){
 std::ostringstream out;const auto result=PrintFunctionTree(t,out);Check(std::holds_alternative<PrintedFunction>(result));
 const auto& positions=std::get<PrintedFunction>(result);Check(positions.bytes==out.str().size());
 for(const auto& span:positions.expressions)Check(span.begin<=span.end&&span.end<=positions.bytes);
 CountingSink sink;std::ostream counted(&sink);const auto count=PrintFunctionTree(t,counted);Check(std::holds_alternative<PrintedFunction>(count));Check(sink.Bytes()==positions.bytes);
 return out.str();
}
}
int main(int argc,char** argv){
 if(argc==2){std::string_view form=argv[1];std::cout<<Print((form.starts_with("object")||form=="lethead"||form=="voidconstruct")?Head(form):Make(form));return 0;}
 for(auto form:{"value","member","base","sign","updatebase"}){
   auto text=Print(Make(form));
   if(std::string_view(form)=="sign") Check(text.find("+(++x)")!=std::string::npos);
   else if(std::string_view(form)=="updatebase") Check(text.find("(x++).p")!=std::string::npos);
   else Check(text.find("flag ? {p: x++}")!=std::string::npos);
   Check(text.find("x = x + 1, {}")!=std::string::npos);
   if(std::string_view(form)=="base")Check(text.find("(x = []).push(")!=std::string::npos);
 }
 // Value-call removal is restricted to the final plain non-eval binding.
 Check(Print(Make("value")).find("return f(")!=std::string::npos);
 {auto t=Make("value");t.strictness=Strictness::kSloppy;
  Check(Print(t).find("(0, f)(")!=std::string::npos);}
 {auto t=Make("value");t.names[0]=u"renamed";
  Check(Print(t).find("return renamed(")!=std::string::npos);}
 {auto t=Make("value");t.names[0]=u"eval";
  std::ostringstream denied;Check(std::holds_alternative<TreePrintFailure>(PrintFunctionTree(t,denied)));}
 {auto t=Make("value");t.expressions[0].expression=GlobalNameExpr{{0}};t.names[0]=u"eval";
  // Existing global-reference admission rejects eval. Retain that typed
  // refusal; the independent token tests cover the required wrapper.
  t.names.push_back(u"unused");t.symbols[0].name={5};
  std::ostringstream denied;Check(std::holds_alternative<TreePrintFailure>(PrintFunctionTree(t,denied)));}
 {auto t=Make("member");std::get<CallExpr>(t.expressions.back().expression).reference=CallReference::kValue;
  Check(Print(t).find("(0, f.push)(")!=std::string::npos);}
 // Current binding validation rejects reserved `let`, even in a sloppy tree.
 // Test the lexical boundary independently without weakening that admission.
 {std::ostringstream denied;Check(std::holds_alternative<TreePrintFailure>(PrintFunctionTree(Head("lethead"),denied)));}
 Check(Print(Head("voidconstruct")).find("new f(void 0, {})")!=std::string::npos);
 Check(Print(Head("objectstatement")).find("({p: x}).p;")!=std::string::npos);
 Check(Print(Head("objectvalue")).find("return {p: x}.p;")!=std::string::npos);
 for(auto n:{200u,201u,400u,800u}){auto text=Print(Make("value",n));Check(!text.empty());auto literals=Print(Make("void",n));Check(literals.find("(void 0)")==std::string::npos);}
}
