#include "../../../src/source/binding_initializer.h"
#include "projected_ranges.h"
#include "../../../src/source/loop_syntax.h"
#include "../../../src/source/nullish_syntax.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>
using namespace abcd;
using namespace abcd::source;
namespace {
// Two owned syntax encodings of the same initialized value. Negative cases
// retain TDZ/const/source-cell obligations; layout is only a test perturbation.
struct Fixture {
  FunctionTree tree{};
  ExprId Expr(TreeExpression node) {
    ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};
    tree.expressions.push_back({std::move(node),{0}});return id;
  }
  StmtId Stmt(TreeStatement node) {
    StmtId id{static_cast<std::uint32_t>(tree.statements.size())};
    tree.statements.push_back({std::move(node),{0}});return id;
  }
  ExprId Read(unsigned binding) {return Expr(SymbolExpr{{binding}});}
  ExprId Call(unsigned binding,std::optional<unsigned> argument={}) {
    const auto first=static_cast<std::uint32_t>(tree.arguments.size());
    if(argument)tree.arguments.push_back(Read(*argument));
    return Expr(CallExpr{Read(binding),{first,argument?1U:0U},CallReference::kValue});
  }
  StmtId Write(unsigned binding,ExprId value) {
    return Stmt(ExpressionStmt{Expr(AssignmentExpr{SymbolTarget{{binding}},value})});
  }
  StmtId Block(std::vector<StmtId> children,ScopeId scope) {
    const auto first=static_cast<std::uint32_t>(tree.children.size());
    tree.children.insert(tree.children.end(),children.begin(),children.end());
    return Stmt(BlockStmt{scope,first,static_cast<std::uint32_t>(children.size())});
  }
  Fixture(unsigned form,unsigned count,bool native,bool reverse) {
    tree.callable=CallableKind::kFunction;tree.strictness=Strictness::kStrict;
    tree.parameter_scope={0};tree.body_scope={1};
    tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
    tree.names={u"input",u"seed",u"report",u"flag",u"current",u"scratch",u"error"};
    tree.origins={OriginalSite{{0},{0}}};
    for(unsigned i=0;i<4;++i) {
      tree.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kSynthesized,{}});
      tree.parameters.push_back({{i},{},false});
    }
    for(unsigned i=4;i<6;++i)tree.symbols.push_back({{1},{i},
        form==12||(form==6&&i==5)?BindingKind::kLocal:BindingKind::kCompilerTemporary,
        form==6&&i==5?DeclarationKind::kConst:DeclarationKind::kLet,
        NameEvidence::kSynthesized,form==12||(form==6&&i==5)?std::optional<TemporaryReason>{}:TemporaryReason::kMultipleUses});
    std::vector<StmtId> body;
    const auto initialize=[&](unsigned binding,ExprId value) {
      if(native || tree.symbols[binding].declaration==DeclarationKind::kConst)
        body.push_back(Stmt(DeclarationStmt{{binding},value}));
      else {body.push_back(Stmt(DeclarationStmt{{binding},{}}));body.push_back(Write(binding,value));}
    };
    if(form==8) {
      // Both encodings here are actual lexical initialization: a TDZ read is
      // deliberately not equated with reading an earlier empty let's value.
      body.push_back(Stmt(DeclarationStmt{{4},Read(4)}));
      body.push_back(Stmt(DeclarationStmt{{5},{}}));
    } else {
      auto value=form==0?Expr(TreeLiteral{IntegerLiteral{42}}):
          form==3||form==4||form==5||form==7||form==12||(form>=17&&form<22&&form!=20)?Call(1):
          form==11?Expr(EmptyObjectExpr{}):form==20||form==27?Call(1,4):form==25||form==28?Call(1):Read(0);
      if(form==2||form==26)value=Expr(SequenceExpr{Call(2,0),value});
      initialize(4,value);
      if(form==7)body.push_back(Stmt(ExpressionStmt{Call(2,5)}));
      if(form==15)body.push_back(Write(5,Call(1)));
      if(form==3||form==4||form==11||form==12)body.push_back(Stmt(DeclarationStmt{{5},{}}));
      else initialize(5,(form==22||form==23)?Call(1):form==16?Expr(SequenceExpr{Call(2,0),Expr(TreeLiteral{IntegerLiteral{42}})}):Read(4));
    }
    for(unsigned i=0;i<count;++i) {
      const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};
      tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});body.push_back(Block({},scope));
    }
    if(form==5||form==13||form==14||form==15) {
      const auto update=Stmt(ExpressionStmt{Expr(UpdateExpr{{5},UpdateDirection::kIncrement,UpdateResult::kPrefix})});
      const auto copy=Write(4,Read(5));
      if(form==13||form==14) {
        const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};
        tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
        if(form==13)body.push_back(Stmt(IfStmt{Read(3),Block({update,copy},scope),{}}));
        else {
          const auto loop=Stmt(LoopStmt{{0},{}});
          const ScopeId arm_scope{static_cast<std::uint32_t>(tree.scopes.size())};
          tree.scopes.push_back({ScopeKind::kBlock,scope});
          const ScopeId exit_scope{static_cast<std::uint32_t>(tree.scopes.size())};
          tree.scopes.push_back({ScopeKind::kBlock,scope});
          const auto guard=Stmt(IfStmt{Call(3),Block({update,copy},arm_scope),Block({Stmt(BreakStmt{loop,{}})},exit_scope)});
          std::get<LoopStmt>(tree.statements[loop.value].statement).body=Block({guard},scope);
          body.push_back(loop);
        }
      } else {body.push_back(update);body.push_back(copy);}
    }
    if(form==18)body.push_back(Write(5,Call(1)));
    if(form==19)body.push_back(Write(4,Call(1)));
    if(form==21) {
      const auto loop=Stmt(LoopStmt{{0},{}});
      const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};
      tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
      const ScopeId arm{static_cast<std::uint32_t>(tree.scopes.size())};
      tree.scopes.push_back({ScopeKind::kBlock,scope});
      const ScopeId exit{static_cast<std::uint32_t>(tree.scopes.size())};
      tree.scopes.push_back({ScopeKind::kBlock,scope});
      const auto guard=Stmt(IfStmt{Call(3),Block({Write(5,Call(1))},arm),Block({Stmt(BreakStmt{loop,{}})},exit)});
      std::get<LoopStmt>(tree.statements[loop.value].statement).body=Block({guard},scope);body.push_back(loop);
    }
    if(form==22||form==23) {
      std::vector<StmtId> writes{Write(5,Call(1)),Stmt(ExpressionStmt{Call(2,5)}),Write(4,Read(5))};
      if(form==22)body.insert(body.end(),writes.begin(),writes.end());
      else {
        const ScopeId protected_scope{static_cast<std::uint32_t>(tree.scopes.size())};
        tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
        const ScopeId catch_scope{static_cast<std::uint32_t>(tree.scopes.size())};
        tree.scopes.push_back({ScopeKind::kCatch,ScopeId{1}});
        tree.symbols.push_back({catch_scope,{6},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kSynthesized,{}});
        const auto handler=Block({Stmt(ExpressionStmt{Call(2,4)})},catch_scope);
        body.push_back(Stmt(TryStmt{Block(writes,protected_scope),CatchClause{{6},handler},{}}));
      }
    }
    if(form>=24)body.push_back(Write(4,Call(1)));
    if(form==16) {body.push_back(Stmt(ExpressionStmt{Call(2,5)}));body.push_back(Write(4,Read(5)));}
    if(form==10) {
      const ScopeId protected_scope{static_cast<std::uint32_t>(tree.scopes.size())};
      tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
      const ScopeId catch_scope{static_cast<std::uint32_t>(tree.scopes.size())};
      tree.scopes.push_back({ScopeKind::kCatch,ScopeId{1}});
      tree.symbols.push_back({catch_scope,{6},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kSynthesized,{}});
      const auto protected_body=Block({Write(5,Call(1)),Write(4,Read(5))},protected_scope);
      const auto handler=Block({Stmt(ExpressionStmt{Call(2,4)})},catch_scope);
      body.push_back(Stmt(TryStmt{protected_body,CatchClause{{6},handler},{}}));
    }
    const unsigned result=form==17||form==21?0:form==18?5:form==3||form==4||form==5||form==8||form==10||form==11||form==12||form>=13?4:5;
    std::vector<StmtId> tail;
    if(form!=3&&form!=17&&form!=20&&form!=21&&form!=28)tail.push_back(Stmt(ExpressionStmt{Call(2,result)}));
    tail.push_back(Stmt(ReturnStmt{Read(result)}));
    if(form==9) {
      const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};
      tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
      tree.symbols.push_back({scope,{0},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kSynthesized,{}});
      tail.insert(tail.begin(),Stmt(DeclarationStmt{{6},Expr(TreeLiteral{IntegerLiteral{17}})}));
      body.push_back(Block(tail,scope));
    } else body.insert(body.end(),tail.begin(),tail.end());
    tree.body=Block(body,{1});
    if(reverse) {
      const auto remap=[&](StmtId id){return StmtId{static_cast<std::uint32_t>(tree.statements.size()-1-id.value)};};
      for(auto& id:tree.children)id=remap(id);
      for(auto& statement:tree.statements)std::visit([&](auto& syntax) {
        using T=std::decay_t<decltype(syntax)>;
        if constexpr(std::is_same_v<T,TryStmt>){syntax.body=remap(syntax.body);syntax.handler->body=remap(syntax.handler->body);}
        else if constexpr(std::is_same_v<T,IfStmt>){syntax.consequent=remap(syntax.consequent);if(syntax.alternate)syntax.alternate=remap(*syntax.alternate);}
        else if constexpr(std::is_same_v<T,LoopStmt>)syntax.body=remap(syntax.body);
        else if constexpr(std::is_same_v<T,BreakStmt>)syntax.target=remap(syntax.target);
      },statement.statement);
      tree.body=remap(tree.body);std::reverse(tree.statements.begin(),tree.statements.end());
      const auto expression=[&](ExprId id){return ExprId{static_cast<std::uint32_t>(tree.expressions.size()-1-id.value)};};
      const auto symbol=[&](SymbolId id){return SymbolId{static_cast<std::uint32_t>(tree.symbols.size()-1-id.value)};};
      for(auto& node:tree.expressions)std::visit([&](auto& syntax) {
        using T=std::decay_t<decltype(syntax)>;
        if constexpr(std::is_same_v<T,CallExpr>)syntax.callee=expression(syntax.callee);
        else if constexpr(std::is_same_v<T,SequenceExpr>){syntax.left=expression(syntax.left);syntax.right=expression(syntax.right);}
        else if constexpr(std::is_same_v<T,AssignmentExpr>){syntax.value=expression(syntax.value);std::get<SymbolTarget>(syntax.target).symbol=symbol(std::get<SymbolTarget>(syntax.target).symbol);}
        else if constexpr(std::is_same_v<T,UpdateExpr>||std::is_same_v<T,SymbolExpr>)syntax.symbol=symbol(syntax.symbol);
      },node.expression);
      for(auto& argument:tree.arguments)argument=expression(argument);
      for(auto& node:tree.statements)std::visit([&](auto& syntax) {
        using T=std::decay_t<decltype(syntax)>;
        if constexpr(std::is_same_v<T,ExpressionStmt>)syntax.expression=expression(syntax.expression);
        else if constexpr(std::is_same_v<T,DeclarationStmt>){syntax.symbol=symbol(syntax.symbol);if(syntax.initializer)syntax.initializer=expression(*syntax.initializer);}
        else if constexpr(std::is_same_v<T,ReturnStmt>){if(syntax.value)syntax.value=expression(*syntax.value);}
        else if constexpr(std::is_same_v<T,IfStmt>)syntax.condition=expression(syntax.condition);
        else if constexpr(std::is_same_v<T,TryStmt>)syntax.handler->parameter=symbol(syntax.handler->parameter);
      },node.statement);
      for(auto& parameter:tree.parameters)parameter.symbol=symbol(parameter.symbol);
      std::reverse(tree.expressions.begin(),tree.expressions.end());std::reverse(tree.symbols.begin(),tree.symbols.end());
    }
  }
};
}
int main(int argc,char**) {
  for(unsigned form=0;form<29;++form)for(bool native:{false,true}) {
    std::uint64_t small=0,large=0;
    for(unsigned count:{0U,1U,200U,201U,400U,800U})for(bool reverse:{false,true}) {
      Fixture fixture(form,count,native,reverse);std::ostringstream output;
      const auto checked=VerifyTreeStructure(fixture.tree);
      if(!std::holds_alternative<TreeStructure>(checked))std::cerr<<"tree form="<<form<<" error="<<unsigned(std::get<TreeStructureError>(checked))<<'\n';
      test::Check(std::holds_alternative<TreeStructure>(checked));
      const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(fixture.tree));
      const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(fixture.tree));
      const auto planned=PlanBindingInitializers(fixture.tree,loops,nullish);
      test::Check(std::holds_alternative<BindingInitializers>(planned));
      if(!reverse&&count==400)small=std::get<BindingInitializers>(planned).work;
      if(!reverse&&count==800)large=std::get<BindingInitializers>(planned).work;
      test::Check(std::get<TreeStructureError>(PlanBindingInitializers(fixture.tree,loops,nullish,{0}))==TreeStructureError::kResourceLimit);
      const auto printed=PrintFunctionTree(fixture.tree,output);
      test::Check(std::holds_alternative<PrintedFunction>(printed));
      test::CheckProjectedRanges(fixture.tree,std::get<BindingInitializers>(planned),std::get<PrintedFunction>(printed),output.str().size());
      if(native&&(form==24||form==25||form==26)) {
        for(unsigned i=0;i<fixture.tree.statements.size();++i)if(const auto* declaration=std::get_if<DeclarationStmt>(&fixture.tree.statements[i].statement))
          if(declaration->initializer&&fixture.tree.names[fixture.tree.symbols[declaration->symbol.value].name.value]==u"current") {
            const auto& plan=std::get<BindingInitializers>(planned);
            if(form==24)test::Check(plan.omitted[i]);
            else test::Check(plan.declaration_values[i]&&!plan.declaration_values[i]->target);
          }
      }
      if(form==22&&native) {
        const auto& plan=std::get<BindingInitializers>(planned);
        bool projected=false;
        for(const auto& value:plan.declaration_values)if(value) {
          projected=true;
          if(value->target)std::cerr<<"dead projected store form="<<form<<" reverse="<<reverse<<'\n'<<output.str()<<'\n';
          test::Check(!value->target);
        }
        test::Check(projected);
      }
      if(form<=3||form==17||form==21||form==28) {
        if(output.str().find("current")!=std::string::npos||output.str().find("scratch")!=std::string::npos)
          std::cerr<<"form="<<form<<" native="<<native<<" reverse="<<reverse<<'\n'<<output.str()<<'\n';
        test::Check(output.str().find("current")==std::string::npos&&output.str().find("scratch")==std::string::npos);
      }
      if(argc>1)std::cout<<nlohmann::json{{"source",output.str()},{"form",form},{"native",native},{"count",count},{"reverse",reverse}}<<'\n';
    }
    test::Check(large<small*3);
  }
}
