#include "../../../src/source/binding_initializer.h"
#include "projected_ranges.h"
#include "../../../src/source/loop_syntax.h"
#include "../../../src/source/nullish_syntax.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
#include <algorithm>
#include <bit>
#include <limits>
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
    tree.names={u"input",u"seed",u"report",u"flag",u"current",u"scratch",u"error",u"value"};
    if(form>=29)tree.texts={u"text",form==39?u"other":u"text",u""};
    if(form==38||form==39)for(unsigned i=0;i<count;++i)
      tree.texts.push_back(std::u16string(64,u'x')+std::u16string(1,static_cast<char16_t>(i+1)));
    tree.origins={OriginalSite{{0},{0}}};
    for(unsigned i=0;i<4;++i) {
      tree.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kSynthesized,{}});
      tree.parameters.push_back({{i},{},false});
    }
    for(unsigned i=4;i<6;++i)tree.symbols.push_back({{1},{i},
        form==12||form==96||(form==6&&i==5)?BindingKind::kLocal:BindingKind::kCompilerTemporary,
        form==6&&i==5?DeclarationKind::kConst:DeclarationKind::kLet,
        NameEvidence::kSynthesized,form==12||form==96||(form==6&&i==5)?std::optional<TemporaryReason>{}:TemporaryReason::kMultipleUses});
    std::vector<StmtId> body;
    const auto initialize=[&](unsigned binding,ExprId value) {
      if(native || tree.symbols[binding].declaration==DeclarationKind::kConst)
        body.push_back(Stmt(DeclarationStmt{{binding},value}));
      else {body.push_back(Stmt(DeclarationStmt{{binding},{}}));body.push_back(Write(binding,value));}
    };
    if(form>=89) {
      const auto integer=[&](std::int32_t value){return Expr(TreeLiteral{IntegerLiteral{value}});};
      const auto report=[&]{return Stmt(ExpressionStmt{Call(2,4)});};
      const auto scope=[&](ScopeKind kind=ScopeKind::kBlock) {
        const ScopeId id{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({kind,ScopeId{1}});return id;
      };
      if(form==95)body.push_back(report()); // TDZ is not a primitive normal read.
      if(form==94)body.push_back(Stmt(DeclarationStmt{{4},{}}));else initialize(4,form==98?Expr(TreeLiteral{BooleanLiteral{false}}):integer(42));
      body.push_back(report());
      if(form==90||form==91)body.push_back(Stmt(IfStmt{Read(3),Block({Write(4,integer(43)),report()},scope()),
          Block({Write(4,integer(form==90?44:43)),report()},scope())}));
      else if(form==92) {
        const auto protected_scope=scope(),catch_scope=scope(ScopeKind::kCatch);
        tree.symbols.push_back({catch_scope,{6},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kSynthesized,{}});
        body.push_back(Stmt(TryStmt{Block({Write(4,Expr(SequenceExpr{Call(2,0),integer(43)})),report()},protected_scope),
            CatchClause{{6},Block({report()},catch_scope)},{}}));
      }else if(form==93) {
        const auto loop=Stmt(LoopStmt{});const auto loop_scope=scope();
        const ScopeId exit{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kBlock,loop_scope});
        const auto test=Stmt(IfStmt{Expr(UnaryExpr{TreeUnary::kNot,Call(3)}),Block({Stmt(BreakStmt{loop,{}})},exit),{}});
        std::get<LoopStmt>(tree.statements[loop.value].statement).body=Block({test,Write(4,integer(43)),report()},loop_scope);body.push_back(loop);
      }else if(form==97)body.push_back(Stmt(ExpressionStmt{Expr(UpdateExpr{{4},UpdateDirection::kIncrement,UpdateResult::kPrefix})}));
      else {
        if(form==89)for(unsigned i=0;i<count;++i){body.push_back(Write(4,integer(static_cast<std::int32_t>(i+50))));body.push_back(report());}
        body.push_back(Write(4,integer(43)));
      }
      if(form==98) {
        body.push_back(Write(4,Expr(TreeLiteral{BooleanLiteral{true}})));
        initialize(5,Expr(ConditionalExpr{Read(4),Expr(SequenceExpr{Call(2,0),integer(43)}),Call(1)}));
      }else {
        if(form==99)body.push_back(Write(4,Call(1)));
        initialize(5,Read(4));
      }
    } else if(form>=81) {
      const auto literal=[&](TreeLiteral value){return Expr(std::move(value));};
      initialize(4,form==82?literal(NullLiteral{}):form==86?Read(3):
          form==87?literal(IntegerLiteral{0}):literal(BooleanLiteral{form==84||form==85}));
      unsigned previous=4;
      if(form==88)for(unsigned i=0;i<count;++i) {
        const auto binding=static_cast<std::uint32_t>(tree.symbols.size()),name=static_cast<std::uint32_t>(tree.names.size());
        const auto digits=std::to_string(i);tree.names.push_back(u"chain"+std::u16string(digits.begin(),digits.end()));
        tree.symbols.push_back({{1},{name},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
        initialize(binding,Expr(ConditionalExpr{Read(previous),Call(1),literal(BooleanLiteral{false})}));previous=binding;
      }
      auto condition=form==87?Expr(UpdateExpr{{4},UpdateDirection::kIncrement,UpdateResult::kPrefix}):Read(previous);
      ExprId value;
      if(form>=82&&form<=84)value=Expr(LogicalExpr{form==82?TreeLogical::kNullish:form==83?TreeLogical::kOr:TreeLogical::kAnd,condition,literal(IntegerLiteral{42})});
      else value=Expr(ConditionalExpr{condition,form==85?Expr(SequenceExpr{Call(2,0),literal(IntegerLiteral{42})}):form==86||form==87?literal(IntegerLiteral{43}):Call(1),form==85?Call(1):literal(IntegerLiteral{42})});
      initialize(5,value);
    } else if(form>=74) {
      if(form==79)body.push_back(Stmt(ExpressionStmt{Call(2,4)}));
      body.push_back(Stmt(DeclarationStmt{{4},{}}));
      if(form==80) {
        const ScopeId arm{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
        body.push_back(Stmt(IfStmt{Read(3),Block({Write(4,Read(0))},arm),{}}));
      }
      ExprId value=Read(4);
      if(form==75||form==76||form==77)value=Expr(SequenceExpr{Call(2,0),value});
      if(form==75)value=Expr(LogicalExpr{TreeLogical::kNullish,value,Read(0)});
      if(form==76)value=Expr(ConditionalExpr{value,Call(1),Read(0)});
      initialize(5,value);
      if(form==78) {
        const auto loop=Stmt(LoopStmt{});const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
        const ScopeId exit{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kBlock,scope});
        const auto test=Stmt(IfStmt{Expr(UnaryExpr{TreeUnary::kNot,Call(3)}),Block({Stmt(BreakStmt{loop,{}})},exit),{}});
        std::get<LoopStmt>(tree.statements[loop.value].statement).body=Block({test,Write(4,Read(5)),Write(5,Read(4))},scope);body.push_back(loop);
      }
    } else if(form==8) {
      // Both encodings here are actual lexical initialization: a TDZ read is
      // deliberately not equated with reading an earlier empty let's value.
      body.push_back(Stmt(DeclarationStmt{{4},Read(4)}));
      body.push_back(Stmt(DeclarationStmt{{5},{}}));
    } else {
      ExprId value{};if(form<29)value=form==0?Expr(TreeLiteral{IntegerLiteral{42}}):
          form==3||form==4||form==5||form==7||form==12||(form>=17&&form<22&&form!=20)?Call(1):
          form==11?Expr(EmptyObjectExpr{}):form==20||form==27?Call(1,4):form==25||form==28?Call(1):Read(0);
      if(form==2||form==26)value=Expr(SequenceExpr{Call(2,0),value});
      if(form>=29) {
        const auto integer=[&](std::int32_t number){return Expr(TreeLiteral{IntegerLiteral{number}});};
        const auto number=[&](double number){return Expr(TreeLiteral{NumberLiteral{std::bit_cast<std::uint64_t>(number)}});};
        const auto effect=[&](ExprId value,unsigned function){return Expr(SequenceExpr{Call(function,0),value});};
        const auto choice=[&](ExprId condition,ExprId a,ExprId b){return Expr(ConditionalExpr{condition,effect(a,2),effect(b,1)});};
        switch(form) {
          case 29:case 30:value=choice(Read(3),integer(42),integer(form==30?43:42));break;
          case 31:value=Expr(AssignmentExpr{SymbolTarget{{0}},effect(integer(42),2)});break;
          case 32:case 33:value=choice(Read(3),number(-0.0),number(form==33?0.0:-0.0));break;
          case 34:case 35:case 36:case 37:
            value=Expr(LogicalExpr{form==34?TreeLogical::kOr:form==35?TreeLogical::kAnd:TreeLogical::kNullish,
                effect(form==36?Expr(TreeLiteral{NullLiteral{}}):integer(0),2),effect(integer(42),1)});break;
          case 38:case 39:value=choice(Read(3),Expr(TreeLiteral{TextId{0}}),Expr(TreeLiteral{TextId{1}}));break;
          case 40:value=choice(Call(1),integer(42),integer(42));break;
          case 41:value=Expr(SequenceExpr{Read(4),integer(42)});break;
          case 42:value=Expr(SequenceExpr{Expr(AssignmentExpr{SymbolTarget{{0}},effect(integer(42),2)}),Expr(TreeLiteral{TextId{0}})});break;
          case 43:value=choice(Read(3),number(std::numeric_limits<double>::quiet_NaN()),number(std::numeric_limits<double>::quiet_NaN()));break;
          case 44:value=choice(Read(3),Expr(TreeLiteral{BooleanLiteral{true}}),Expr(TreeLiteral{BooleanLiteral{true}}));break;
          case 45:value=choice(Read(3),Expr(TreeLiteral{UndefinedLiteral{}}),Expr(TreeLiteral{UndefinedLiteral{}}));break;
          case 46:case 47:value=Expr(LogicalExpr{form==46?TreeLogical::kOr:TreeLogical::kAnd,
                effect(Expr(TreeLiteral{TextId{form==46?2U:0U}}),2),effect(integer(42),1)});break;
          case 48:value=integer(42);break;
          case 49:value=Expr(AssignmentExpr{MemberTarget{Read(1),NameId{7}},effect(integer(42),2)});break;
          case 50:case 51:value=Expr(LogicalExpr{form==50?TreeLogical::kOr:TreeLogical::kAnd,
                effect(number(std::numeric_limits<double>::quiet_NaN()),2),effect(integer(42),1)});break;
          case 52:value=choice(effect(Expr(TreeLiteral{BooleanLiteral{false}}),2),integer(42),integer(43));break;
          case 53:value=Expr(LogicalExpr{TreeLogical::kOr,effect(integer(-1),2),effect(integer(42),1)});break;
          case 54:value=Expr(LogicalExpr{TreeLogical::kAnd,effect(number(std::numeric_limits<double>::infinity()),2),effect(integer(42),1)});break;
          case 55:value=Expr(LogicalExpr{TreeLogical::kNullish,effect(Expr(TreeLiteral{UndefinedLiteral{}}),2),effect(integer(42),1)});break;
          case 56:value=choice(effect(number(-0.0),2),integer(42),integer(43));break;
          case 57:case 58:value=Expr(ConditionalExpr{effect(Expr(TreeLiteral{BooleanLiteral{form==57}}),2),effect(integer(42),1),Call(1)});break;
          case 59:value=effect(integer(42),2);break;
          case 60:case 61:case 62:case 63:case 64:case 65:value=Read(0);break;
          case 66:case 67:value=Expr(TreeLiteral{BooleanLiteral{form==66}});break;
          case 68:case 69:case 70:case 71:case 72:value=integer(42);break;
          case 73:value=integer(0);break;
          default:test::Check(false);
        }
      }
      if(form==59) {
        body.push_back(Stmt(DeclarationStmt{{4},{}}));
        const ScopeId protected_scope{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
        const ScopeId catch_scope{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kCatch,ScopeId{1}});
        tree.symbols.push_back({catch_scope,{6},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kSynthesized,{}});
        body.push_back(Stmt(TryStmt{Block({Write(4,value)},protected_scope),
            CatchClause{{6},Block({Stmt(ExpressionStmt{Call(2,4)})},catch_scope)},{}}));
      } else initialize(4,value);
      if(form==68||form==69) {
        const ScopeId a{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
        const ScopeId b{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
        body.push_back(Stmt(IfStmt{Read(3),Block({Write(4,Expr(TreeLiteral{IntegerLiteral{42}}))},a),
            Block({Write(4,Expr(TreeLiteral{IntegerLiteral{form==68?42:43}}))},b)}));
      }
      const auto carried=[&](std::vector<StmtId> writes) {
        const auto loop=Stmt(LoopStmt{});const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
        const ScopeId exit{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kBlock,scope});
        writes.insert(writes.begin(),Stmt(IfStmt{Expr(UnaryExpr{TreeUnary::kNot,Call(3)}),Block({Stmt(BreakStmt{loop,{}})},exit),{}}));
        std::get<LoopStmt>(tree.statements[loop.value].statement).body=Block(writes,scope);body.push_back(loop);
      };
      if(form==70||form==71)carried({Write(4,form==70?Read(4):Expr(TreeLiteral{IntegerLiteral{43}}))});
      if(form==7)body.push_back(Stmt(ExpressionStmt{Call(2,5)}));
      if(form==15)body.push_back(Write(5,Call(1)));
      if(form==3||form==4||form==11||form==12)body.push_back(Stmt(DeclarationStmt{{5},{}}));
      else {
        ExprId copy{};if(form<60)copy=(form==22||form==23)?Call(1):form==16?Expr(SequenceExpr{Call(2,0),Expr(TreeLiteral{IntegerLiteral{42}})}):
            form==48?Expr(SequenceExpr{Call(2,0),Read(4)}):Read(4);
        if(form>=60) {
          // These cases consume a literal fact committed by an earlier pass.
          const auto primitive=[&](TreeLiteral literal){return Expr(std::move(literal));};
          const auto effect=[&](ExprId value){return Expr(SequenceExpr{Call(2,0),value});};
          if(form==73)copy=Expr(UpdateExpr{{4},UpdateDirection::kIncrement,UpdateResult::kPrefix});
          else if(form>=68)copy=Read(4);
          else if(form==60||form==65)copy=Expr(ConditionalExpr{effect(primitive(BooleanLiteral{form==60})),Read(4),Call(1)});
          else if(form==66||form==67)copy=Expr(ConditionalExpr{effect(Read(4)),Read(0),Call(1)});
          else copy=Expr(LogicalExpr{form==61?TreeLogical::kOr:form==63?TreeLogical::kNullish:TreeLogical::kAnd,
              effect(form==63?primitive(NullLiteral{}):form==64?primitive(BooleanLiteral{false}):primitive(IntegerLiteral{form==61?0:42})),Read(4)});
        }
        initialize(5,copy);
      }
      if(form==72)carried({Write(4,Read(5)),Write(5,Read(4))});
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
    if(form>=24&&form<29)body.push_back(Write(4,Call(1)));
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
    const unsigned result=form==17||form==21?0:form==18?5:form==3||form==4||form==5||form==8||form==10||form==11||form==12||(form>=13&&form<29)?4:5;
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
        if constexpr(std::is_same_v<T,TreeLiteral>) {if(auto* text=std::get_if<TextId>(&syntax))text->value=static_cast<std::uint32_t>(tree.texts.size()-1-text->value);}
        else if constexpr(std::is_same_v<T,CallExpr>)syntax.callee=expression(syntax.callee);
        else if constexpr(std::is_same_v<T,SequenceExpr>){syntax.left=expression(syntax.left);syntax.right=expression(syntax.right);}
        else if constexpr(std::is_same_v<T,ConditionalExpr>){syntax.condition=expression(syntax.condition);syntax.consequent=expression(syntax.consequent);syntax.alternate=expression(syntax.alternate);}
        else if constexpr(std::is_same_v<T,LogicalExpr>){syntax.left=expression(syntax.left);syntax.right=expression(syntax.right);}
        else if constexpr(std::is_same_v<T,UnaryExpr>)syntax.operand=expression(syntax.operand);
        else if constexpr(std::is_same_v<T,AssignmentExpr>){syntax.value=expression(syntax.value);
          if(auto* target=std::get_if<SymbolTarget>(&syntax.target))target->symbol=symbol(target->symbol);
          else std::get<MemberTarget>(syntax.target).base=expression(std::get<MemberTarget>(syntax.target).base);}
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
      std::reverse(tree.texts.begin(),tree.texts.end());
    }
  }
};
}
int main(int argc,char**) {
  for(unsigned form=0;form<100;++form)for(bool native:{false,true}) {
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
      if(form>=29&&count==0&&!reverse) {
        const auto& plan=std::get<BindingInitializers>(planned);
        test::Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(fixture.tree,loops,nullish,{plan.allocated_bytes,plan.work})));
        test::Check(std::get<TreeStructureError>(PlanBindingInitializers(fixture.tree,loops,nullish,{plan.allocated_bytes-1,plan.work}))==TreeStructureError::kResourceLimit);
        test::Check(std::get<TreeStructureError>(PlanBindingInitializers(fixture.tree,loops,nullish,{plan.allocated_bytes,plan.work-1}))==TreeStructureError::kWorkLimit);
      }
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
      if(form<=3||form==16||form==17||form==21||form==28||(form>=29&&form!=30&&form!=33&&form!=39&&form!=41&&form<58)||
          (form>=60&&form<68&&form!=65&&form!=67)||form==68||form==70||form==72||(form>=74&&form<=78)||(form>=81&&form<=85)||form==88||form==89||form==91||form==94||form==98) {
        if(output.str().find("current")!=std::string::npos||output.str().find("scratch")!=std::string::npos)
          std::cerr<<"form="<<form<<" native="<<native<<" reverse="<<reverse<<'\n'<<output.str()<<'\n';
        test::Check(output.str().find("current")==std::string::npos&&output.str().find("scratch")==std::string::npos);
        if(form==88)test::Check(output.str().find("chain")==std::string::npos);
      }
      if(form==96)test::Check(output.str().find("current")!=std::string::npos);
      if(argc>1)std::cout<<nlohmann::json{{"source",output.str()},{"form",form},{"native",native},{"count",count},{"reverse",reverse}}<<'\n';
    }
    test::Check(large<small*3);
  }
}
