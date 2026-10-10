#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_structure.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/source_variables.h"
#include "../../../src/source/original_flow.h"
#include "../../../src/source/function_abi.h"
#include "../../../src/source/tree_globals.h"
#include "abcd/source/tree_printer.h"
#include <sstream>

#include <cstdlib>
#include <type_traits>
#include <utility>
#include <iostream>
#include <source_location>
#include <nlohmann/json.hpp>
#include <algorithm>
#include "../../../src/source/recovery_flow.h"
#include "../../../src/source/recovery_projection.h"
#include "../../../src/source/flow_kinds.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/analysis/phased_value_import.h"
#include "../../support/analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool condition, const std::source_location& at = std::source_location::current()) {
  if (!condition) { std::cerr << "function tree check " << at.line() << '\n'; std::abort(); }
}
class Fixture {
 public:
  Fixture() {
    graph.values.push_back({analysis::EntryDefinition{analysis::ParameterEntry{0}},
                            analysis::ValueKinds::Language(), {}});
    graph.values.push_back({analysis::EntryDefinition{analysis::ReceiverEntry{}},
                            analysis::ValueKinds::Language(), {}});
  }
  analysis::OpId Add(std::uint32_t block, std::uint32_t order, std::uint32_t pc,
      analysis::ValueOperation operation, bool result,
      std::initializer_list<std::pair<analysis::OperandRole, analysis::ValueOperand>> operands,
      analysis::LanguageEffect effects = analysis::LanguageEffect::kNone) {
    return Add(block, order, pc, operation, result, std::span(operands.begin(), operands.size()), effects);
  }
  analysis::OpId Add(std::uint32_t block, std::uint32_t order, std::uint32_t pc,
      analysis::ValueOperation operation, bool result,
      std::span<const std::pair<analysis::OperandRole, analysis::ValueOperand>> operands,
      analysis::LanguageEffect effects = analysis::LanguageEffect::kNone) {
    const analysis::OpId id{static_cast<std::uint32_t>(graph.ops.size())};
    std::optional<analysis::ValueId> value;
    if (result) {
      value = analysis::ValueId{static_cast<std::uint32_t>(graph.values.size())};
      graph.values.push_back({analysis::OpResult{id}, analysis::ValueKinds::Language(), {}});
    }
    graph.origins.push_back({{id.value}, {pc}, {}});
    graph.ops.push_back({operation, {block}, order,
        {static_cast<std::uint32_t>(graph.uses.size())}, static_cast<std::uint32_t>(operands.size()),
        value, effects, effects, effects == analysis::LanguageEffect::kNone
            ? analysis::ExceptionalCompletion::kImpossible : analysis::ExceptionalCompletion::kMayThrow,
        {id.value}});
    for (const auto& [role, operand] : operands) {
      const analysis::UseId use{static_cast<std::uint32_t>(graph.uses.size())};
      analysis::OperandUse occurrence{id, role, analysis::ReadPurpose::kOperand, operand, {}, {}, {}, {}};
      if (const auto* input = std::get_if<analysis::ValueId>(&operand)) {
        auto& definition = graph.values[input->value];
        occurrence.next = definition.first_use;
        if (occurrence.next) graph.uses[occurrence.next->value].previous = use;
        definition.first_use = use;
      }
      graph.uses.push_back(occurrence);
    }
    if (std::holds_alternative<analysis::ValueNamedRead>(operation) ||
        std::holds_alternative<analysis::ValueGlobalRead>(operation) ||
        std::holds_alternative<analysis::ValueNamedWrite>(operation))
      constants.emplace_back(NameId{1});
    else constants.emplace_back(std::monostate{});
    return id;
  }
  FunctionSyntax Syntax() const {
    FunctionSyntax syntax{};
    syntax.owner = {3};
    syntax.callable = callable;
    syntax.strictness = Strictness::kStrict;
    syntax.names = {u"params", u"pathStack"};
    syntax.parameters.push_back({NameId{0}, NameEvidence::kDebugRange});
    return syntax;
  }
  std::variant<ConstructedFunction, FunctionTreeError> Build(
      analysis::SemanticBudget budget = {}) const {
    auto control = BuildControlPlan(graph);
    auto evaluation = BuildEvaluationPlan(graph);
    Check(std::holds_alternative<ControlPlan>(control));
    Check(std::holds_alternative<EvaluationPlan>(evaluation));
    auto built = BuildFunctionTree(graph, std::get<ControlPlan>(control),
        std::get<EvaluationPlan>(evaluation), Syntax(), {constants, 2, 0, {}}, budget);
    if (const auto* result = std::get_if<ConstructedFunction>(&built)) {
      Check(result->flow.has_value());
      Check(result->flow->value_revision == result->tree.value_revision);
      Check(result->flow->cfg_revision == result->tree.cfg_revision);
      Check(result->flow->expressions.size() == graph.ops.size());
      // Retention must preserve the actual correspondence, not just revisions.
      // Fresh recomputation is a test oracle only; production reuses the proof.
      const auto fresh = std::get<TreeFlow>(VerifyTreeFlow(graph,
          std::get<ControlPlan>(control), std::get<EvaluationPlan>(evaluation), result->tree));
      Check(result->flow->expressions == fresh.expressions);
      Check(result->flow->statements == fresh.statements);
      Check(result->flow->snapshots == fresh.snapshots);
      Check(result->flow->phis == fresh.phis);
      Check(result->flow->cells == fresh.cells);
    }
    return built;
  }
  std::variant<TreeSemantics, TreeSemanticError> Verify(
      const FunctionTree& tree, analysis::SemanticBudget budget = {}) const {
    auto control = BuildControlPlan(graph);
    auto evaluation = BuildEvaluationPlan(graph);
    Check(std::holds_alternative<ControlPlan>(control));
    Check(std::holds_alternative<EvaluationPlan>(evaluation));
    return VerifyTreeSemantics(graph, std::get<ControlPlan>(control),
        std::get<EvaluationPlan>(evaluation), tree, {constants, 2, 0, {}}, budget);
  }
  void ReverseArenas(ConstructedFunction& built) const {
    auto& tree=built.tree;
    const auto expression=[&](ExprId id){return ExprId{static_cast<unsigned>(tree.expressions.size()-1-id.value)};};
    const auto statement=[&](StmtId id){return StmtId{static_cast<unsigned>(tree.statements.size()-1-id.value)};};
    for(auto& node:tree.expressions)std::visit([&](auto& value) {
      using T=std::decay_t<decltype(value)>;
      if constexpr(std::is_same_v<T,UnaryExpr>)value.operand=expression(value.operand);
      else if constexpr(std::is_same_v<T,BinaryExpr>||std::is_same_v<T,SequenceExpr>||std::is_same_v<T,LogicalExpr>) {
        value.left=expression(value.left);value.right=expression(value.right);
      } else if constexpr(std::is_same_v<T,AssignmentExpr>)value.value=expression(value.value);
      else if constexpr(std::is_same_v<T,ConditionalExpr>) {
        value.condition=expression(value.condition);value.consequent=expression(value.consequent);value.alternate=expression(value.alternate);
      } else Check((std::is_same_v<T,TreeLiteral>||std::is_same_v<T,SymbolExpr>));
    },node.expression);
    for(auto& node:tree.statements)std::visit([&](auto& value) {
      using T=std::decay_t<decltype(value)>;
      if constexpr(std::is_same_v<T,DeclarationStmt>) {if(value.initializer)value.initializer=expression(*value.initializer);}
      else if constexpr(std::is_same_v<T,ExpressionStmt>)value.expression=expression(value.expression);
      else if constexpr(std::is_same_v<T,ReturnStmt>) {if(value.value)value.value=expression(*value.value);}
      else if constexpr(std::is_same_v<T,IfStmt>) {
        value.condition=expression(value.condition);value.consequent=statement(value.consequent);
        if(value.alternate)value.alternate=statement(*value.alternate);
      } else Check((std::is_same_v<T,BlockStmt>||std::is_same_v<T,DebuggerStmt>));
    },node.statement);
    for(auto& op:tree.operations)std::visit([&](auto& id) {
      using T=std::decay_t<decltype(id)>;
      if constexpr(std::is_same_v<T,ExprId>)id=expression(id);
      else if constexpr(std::is_same_v<T,StmtId>)id=statement(id);
    },op.node);
    for(auto& child:tree.children)child=statement(child);
    for(auto& anchor:tree.anchors)if(auto* boundary=std::get_if<BlockBoundary>(&anchor.position))boundary->block=statement(boundary->block);
    tree.body=statement(tree.body);
    std::reverse(tree.expressions.begin(),tree.expressions.end());std::reverse(tree.statements.begin(),tree.statements.end());
    // Any canonical AST mutation replaces correspondence at the real boundary;
    // the old annotation is never reused by a layout-variation fixture.
    auto checked=Verify(tree);Check(std::holds_alternative<TreeSemantics>(checked));
    auto semantics=std::get<TreeSemantics>(std::move(checked));
    built.flow=std::move(semantics.flow);built.work+=semantics.work;built.allocated_bytes+=semantics.allocated_bytes;
  }
  CallableKind callable=CallableKind::kMethod;
  analysis::ValueProgram graph;
  std::vector<ExpressionConstant> constants;
};
// Both checked SSA and source ToNumber establish Number through the common
// provider. A plain copy establishes neither; layout cannot choose the result.
void OriginalNumericSnapshots(bool emit=false,unsigned copies=0,bool reverse=false,bool split=false,bool numeric=true,bool arena=false) {
  using namespace analysis;
  Fixture fixture;fixture.callable=CallableKind::kFunction;
  const auto number=fixture.Add(0,0,0,numeric?ValueOperation{ValueUnary{UnaryOperator::kToNumber}}:ValueOperation{ValueCopy{}},true,
      {{OperandRole::kInput,ValueId{0}}},numeric?LanguageEffect::kUserCode:LanguageEffect::kNone);
  fixture.graph.values[fixture.graph.ops[number.value].result->value].domain=numeric?ValueKinds::Number():ValueKinds::Language();
  auto value=*fixture.graph.ops[number.value].result;
  for(unsigned i=0;i<copies;++i) {
    const auto copy=fixture.Add(0,i+1,i+1,ValueCopy{},true,{{OperandRole::kInput,value}});
    value=*fixture.graph.ops[copy.value].result;
    fixture.graph.values[value.value].domain=numeric?ValueKinds::Number():ValueKinds::Language();
  }
  if(split)fixture.Add(0,copies+1,copies+1,ValueJump{},false,{});
  const unsigned branch_block=split?1:0,branch_start=split?copies+2:0;
  const auto comparison=fixture.Add(branch_block,split?0:copies+1,copies+2,ValueBinary{BinaryOperator::kStrictEqual},true,
      {{OperandRole::kLeft,value},{OperandRole::kRight,analysis::IntegerLiteral{0}}});
  fixture.graph.values[fixture.graph.ops[comparison.value].result->value].domain=ValueKinds::Boolean();
  fixture.Add(branch_block,split?1:copies+2,copies+3,ValueBranch{UnaryOperator::kTrueOrNonzeroNumber},false,
      {{OperandRole::kCondition,*fixture.graph.ops[comparison.value].result}});
  const auto unused=fixture.Add(branch_block+1,0,copies+4,ValueBinary{BinaryOperator::kAdd},true,
      {{OperandRole::kLeft,value},{OperandRole::kRight,analysis::IntegerLiteral{1}}},numeric?LanguageEffect::kNone:LanguageEffect::kUserCode);
  fixture.Add(branch_block+1,1,copies+5,ValueReturn{},false,{{OperandRole::kReturnValue,value}});
  fixture.Add(branch_block+2,0,copies+6,ValueReturn{},false,{{OperandRole::kReturnValue,value}});
  if(split) {
    fixture.graph.blocks.push_back({{0},copies+2,{0},1});
    fixture.graph.edges.push_back({{0},{1},FlowEdgeKind::kTaken,0});
  }
  fixture.graph.blocks.push_back({{branch_start},split?2:copies+3,{split?1U:0U},2});
  fixture.graph.blocks.push_back({{unused.value},2,{split?3U:2U},0});
  fixture.graph.blocks.push_back({{unused.value+2},1,{split?3U:2U},0});
  fixture.graph.edges.push_back({{branch_block},{branch_block+2},FlowEdgeKind::kTaken,0});
  fixture.graph.edges.push_back({{branch_block},{branch_block+1},FlowEdgeKind::kFallthrough,1});
  if(reverse) {
    const auto map=[&](ValueId id){return ValueId{static_cast<unsigned>(fixture.graph.values.size()-1-id.value)};};
    for(auto& op:fixture.graph.ops)if(op.result)op.result=map(*op.result);
    for(auto& use:fixture.graph.uses) {
      if(auto* id=std::get_if<ValueId>(&use.operand))*id=map(*id);
      if(auto* id=std::get_if<ValueId>(&use.owner))*id=map(*id);
    }
    std::reverse(fixture.graph.values.begin(),fixture.graph.values.end());
  }
  auto constructed=fixture.Build();Check(std::holds_alternative<ConstructedFunction>(constructed));
  auto built=std::get<ConstructedFunction>(std::move(constructed));
  if(arena)fixture.ReverseArenas(built);
  Check(built.flow->original != nullptr);
  const auto& original=*built.flow->original;
  Check(original.values.size()==fixture.graph.values.size());Check(original.uses.size()==fixture.graph.uses.size());
  Check(original.definitions.size()==built.tree.expressions.size());
  bool successful=false;
  for(const auto& definition:original.definitions)if(definition) {
    Check(definition->input.value<built.tree.expressions.size());
    successful=true;
  }
  if(numeric)Check(successful);
  Check(original.operations[number.value].normal==(numeric?LanguageEffect::kUserCode:LanguageEffect::kNone));
  Check(original.operations[number.value].exceptional==(numeric?ExceptionalCompletion::kMayThrow:ExceptionalCompletion::kImpossible));
  Check(original.values[fixture.graph.ops[number.value].result->value].domain.IsNumber()==numeric);
  std::size_t occurrences=0;
  for(std::size_t i=0;i<original.input_ranges.size();++i) {
    const auto range=original.input_ranges[i];Check(range.first==occurrences);occurrences+=range.count;
    for(std::size_t j=range.first;j<occurrences;++j) {
      const auto& input=original.inputs[j];Check(input.expression.value==i);
      if(input.occurrence)std::visit([&](const auto& value) {
        using T=std::decay_t<decltype(value)>;
        const auto* expected=std::get_if<T>(&fixture.graph.uses[input.occurrence->value].operand);Check(expected!=nullptr);
        if constexpr(std::is_same_v<T,SpecialValue>)Check(value==*expected);
        else Check(value.value==expected->value);
      },input.operand);
    }
  }
  Check(occurrences==original.inputs.size());
  const auto raw=std::get<SourceVariables>(PrepareSourceVariables(built.tree));
  const auto recovered=std::get<SourceVariables>(PrepareSourceVariables(built.tree,{},&original));
  Check(recovered.original==&original);
  const analysis::SemanticBudget exact{recovered.allocated_bytes,recovered.work};
  Check(std::holds_alternative<SourceVariables>(PrepareSourceVariables(built.tree,exact,&original)));
  Check(std::get<TreeStructureError>(PrepareSourceVariables(built.tree,{exact.bytes-1,exact.work},&original))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(PrepareSourceVariables(built.tree,{exact.bytes,exact.work-1},&original))==TreeStructureError::kWorkLimit);
  const auto statement=*built.flow->statements[unused.value];
  Check(bool(raw.bindings.omitted[statement.value])==numeric);
  Check(bool(recovered.bindings.omitted[statement.value])==numeric);
  if(numeric)Check(recovered.bindings.evaluations[built.flow->expressions[number.value]->value]==ExpressionEvaluation::kRequired);
  for(unsigned dimension=0;dimension<4;++dimension) {
    auto wrong=original;
    if(dimension==0)++wrong.value_revision.value;
    if(dimension==1)++wrong.cfg_revision.value;
    if(dimension==2)++wrong.method.value;
    if(dimension==3)wrong.input_ranges.pop_back();
    Check(std::get<TreeStructureError>(PrepareSourceVariables(built.tree,{},&wrong))==TreeStructureError::kOwnership);
  }
  // Check a fresh shared model, not printer-only annotations. Read ranges keep
  // raw use identity; values become available at successful completion only.
  const auto& bindings=raw.bindings;
  LegacyFlowProjection projection{bindings.aliases,bindings.literals,bindings.replacements,
      bindings.omitted,bindings.evaluations,bindings.normal_stores,bindings.statements,{},
      bindings.storage,bindings.undefined_stores,bindings.declaration_values,bindings.declaration_completions};
  projection.original=&original;
  auto flow=std::get<RecoveryFlow>(BuildRecoveryFlow(built.tree,projection));
  Check(flow.original==&original);
  bool retained=false;
  for(const auto& reference:flow.references)if(reference.original) {
    retained=true;const auto id=std::get<ExprId>(reference.site);Check(flow.normal_exits[id.value].has_value());
    for(std::size_t j=0;j<reference.original->count;++j)Check(original.inputs[reference.original->first+j].expression==id);
  }
  Check(retained);
  std::ostringstream output;
  Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(built.tree,output,{}, {},nullptr,&original)));
  if(emit)std::cout<<nlohmann::json{{"source",output.str()},{"numeric",numeric},{"copies",copies},{"reverse",reverse},{"split",split},{"arena",arena},{"work",built.work},{"bytes",built.allocated_bytes},{"operations",fixture.graph.ops.size()},{"values",fixture.graph.values.size()},{"uses",fixture.graph.uses.size()},{"edges",fixture.graph.edges.size()}}<<'\n';
}
void OriginalSuccessfulDomains(unsigned copies=0,bool reverse=false,bool split=false,bool conditional=false,bool emit=false,bool arena=false,bool action=false) {
  using namespace analysis;
  Fixture f;f.callable=CallableKind::kFunction;
  const unsigned prefix=action?1:0;
  if(action)f.Add(0,0,0,ValueDebugger{},false,{});
  const auto add=[&](unsigned block,unsigned order,unsigned pc,ValueOperation operation,bool result,
      std::initializer_list<std::pair<OperandRole,ValueOperand>> operands,LanguageEffect effects=LanguageEffect::kNone) {
    return f.Add(block,order+(block?0:prefix),pc+prefix,operation,result,operands,effects);
  };
  const auto comparison=add(0,0,0,ValueBinary{BinaryOperator::kStrictEqual},true,
      {{OperandRole::kLeft,ValueId{0}},{OperandRole::kRight,analysis::IntegerLiteral{0}}});
  auto boolean=*f.graph.ops[comparison.value].result;
  f.graph.values[boolean.value].domain=ValueKinds::Boolean();
  for(unsigned i=0;i<copies;++i) {
    const auto copy=add(0,i+1,i+1,ValueCopy{},true,{{OperandRole::kInput,boolean}});
    boolean=*f.graph.ops[copy.value].result;f.graph.values[boolean.value].domain=ValueKinds::Boolean();
  }
  std::optional<OpId> branch;
  if(conditional)branch=add(0,copies+1,copies+1,ValueBranch{UnaryOperator::kTrueOrNonzeroNumber},false,{{OperandRole::kCondition,boolean}});
  else if(split)add(0,copies+1,copies+1,ValueJump{},false,{});
  const auto conversion=add(split||conditional?1:0,split||conditional?0:copies+1,copies+2,ValueUnary{UnaryOperator::kToNumber},true,
      {{OperandRole::kInput,conditional?ValueId{0}:boolean}},LanguageEffect::kUserCode);
  const auto number=*f.graph.ops[conversion.value].result;
  f.graph.values[number.value].domain=ValueKinds::FiniteNumber();
  add(split||conditional?1:0,split||conditional?1:copies+2,copies+3,ValueBinary{BinaryOperator::kAdd},true,
      {{OperandRole::kLeft,number},{OperandRole::kRight,analysis::IntegerLiteral{1}}});
  add(split||conditional?1:0,split||conditional?2:copies+3,copies+4,ValueReturn{},false,{{OperandRole::kReturnValue,number}});
  if(conditional) {
    const auto alternate=add(2,0,copies+5,ValueReturn{},false,{{OperandRole::kReturnValue,ValueId{0}}});
    f.graph.blocks={{{0},copies+2+prefix,{0},2},{{copies+2+prefix},3,{2},0},{{alternate.value},1,{2},0}};
    f.graph.edges={{{0},{1},FlowEdgeKind::kTaken,0},{{0},{2},FlowEdgeKind::kFallthrough,1}};
  } else if(split) {
    f.graph.blocks={{{0},copies+2+prefix,{0},1},{{copies+2+prefix},3,{1},0}};
    f.graph.edges={{{0},{1},FlowEdgeKind::kTaken,0}};
  } else f.graph.blocks={{{0},copies+4+prefix,{0},0}};
  if(reverse) {
    const auto map=[&](ValueId id){return ValueId{static_cast<unsigned>(f.graph.values.size()-1-id.value)};};
    for(auto& op:f.graph.ops)if(op.result)op.result=map(*op.result);
    for(auto& use:f.graph.uses) {
      if(auto* id=std::get_if<ValueId>(&use.operand))*id=map(*id);
      if(auto* id=std::get_if<ValueId>(&use.owner))*id=map(*id);
    }
    std::reverse(f.graph.values.begin(),f.graph.values.end());
  }
  const auto construction=f.Build();Check(std::holds_alternative<ConstructedFunction>(construction));
  auto built=std::get<ConstructedFunction>(construction);
  if(arena)f.ReverseArenas(built);
  const auto& tree=built.tree;
  const auto x=tree.expressions.size(),s=tree.statements.size(),b=tree.symbols.size();
  BindingInitializers plan;
  plan.aliases.resize(b);plan.storage.resize(b);plan.read_bindings.resize(b);plan.literals.resize(b);
  plan.read_sources.resize(x);plan.read_literals.resize(x);plan.replacements.resize(x);plan.transports.resize(x);
  plan.omitted.resize(s);plan.statements.resize(s);plan.primitives.push_back(TreeLiteral{BooleanLiteral{true}});
  std::vector<FlowValueEquation> equations(x);std::vector<ExprId> order;
  for(unsigned i=0;i<x;++i) {
    order.push_back({i});if(const auto* read=std::get_if<SymbolExpr>(&tree.expressions[i].expression))equations[i]=FlowBindingValue{read->symbol};
  }
  std::vector<std::uint8_t> private_bindings(b),stable(b);
  for(unsigned i=0;i<b;++i){private_bindings[i]=tree.symbols[i].kind==BindingKind::kCompilerTemporary;stable[i]=tree.symbols[i].kind==BindingKind::kParameter;}
  const auto* original=built.flow->original.get();
  RecoveryExpressionInput input{plan,equations,order,original};
  LegacyFlowProjection projection{plan.aliases,plan.literals,plan.replacements,plan.omitted,{},{},plan.statements,private_bindings};
  projection.stable_entries=stable;
  const auto model_result=PrepareRecoveryProjection(tree,projection,input);
  Check(std::holds_alternative<RecoveryProjection>(model_result));
  const auto model=std::get<RecoveryProjection>(model_result);
  std::vector<ExprId> observed;
  for(const auto& reference:model.flow.references)if(reference.kind==FlowReferenceKind::kRead)
    if(const auto* site=std::get_if<ExprId>(&reference.site);site&&reference.original) {
      const auto range=*reference.original;
      bool finite=false;
      for(std::size_t i=range.first;i<range.first+range.count;++i) {
        const auto& operand=original->inputs[i];Check(operand.expression==*site);
        if(const auto* value=std::get_if<ValueId>(&operand.resolved))
          finite|=original->values[value->value].domain.IsFiniteNumber();
      }
      if(finite){observed.push_back(*site);Check(model.expressions.kinds[site->value].IsFiniteNumber());}
    }
  Check(!observed.empty());
  // Value-only flow clients can lack source boundary correspondence. Keep
  // solving their current values; only the conditional raw range proof is lost.
  auto value_only=model.flow;value_only.statements.clear();
  const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(value_only));
  const auto value_only_result=AnalyzeFlowNormalKinds(tree,value_only,live,input);
  Check(std::holds_alternative<FlowNormalKinds>(value_only_result));
  const auto& unknown_path=std::get<FlowNormalKinds>(value_only_result);
  for(const auto site:observed) {
    Check(unknown_path.expressions[site.value].IsNumber());
    Check(!unknown_path.expressions[site.value].IsFiniteNumber());
  }
  if(action) {
    const auto statement=*original->operations[0].statement;
    Check(!original->operations[0].result&&!original->operations[0].expression);
    auto omitted=plan.omitted;omitted[statement.value]=1;
    auto candidate_plan=BindingPlanView{plan};candidate_plan.omitted=omitted;
    const auto candidate_model=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,projection,{candidate_plan,equations,order,original}));
    const auto boundary=candidate_model.flow.statements[statement.value];
    Check(candidate_model.flow.events[boundary.value].provenance==FlowProvenance::kChanged);
    Check(model.flow.events[model.flow.statements[statement.value].value].provenance==FlowProvenance::kPreserved);
    const SemanticBudget candidate_exact{candidate_model.allocated_bytes,candidate_model.work};
    const RecoveryExpressionInput candidate_input{candidate_plan,equations,order,original};
    Check(std::holds_alternative<RecoveryProjection>(PrepareRecoveryProjection(tree,projection,candidate_input,candidate_exact)));
    Check(std::get<TreeStructureError>(PrepareRecoveryProjection(tree,projection,candidate_input,{candidate_exact.bytes-1,candidate_exact.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PrepareRecoveryProjection(tree,projection,candidate_input,{candidate_exact.bytes,candidate_exact.work-1}))==TreeStructureError::kWorkLimit);
    const auto candidate_live=std::get<FlowLiveness>(AnalyzeFlowLiveness(candidate_model.flow));
    const auto types=std::get<FlowNormalKinds>(AnalyzeFlowNormalKinds(tree,candidate_model.flow,candidate_live,input));
    for(const auto site:observed) {
      Check(types.expressions[site.value].IsNumber());
      Check(!types.expressions[site.value].IsFiniteNumber());
    }
    auto unmodeled=candidate_model.flow;unmodeled.events[boundary.value].provenance=FlowProvenance::kUnmodeled;
    const auto missing_provenance=std::get<FlowNormalKinds>(AnalyzeFlowNormalKinds(tree,unmodeled,candidate_live,input));
    for(const auto site:observed)Check(!missing_provenance.expressions[site.value].IsFiniteNumber());
  }
  ExprId producer{};bool mapped=false;
  for(const auto& definition:original->definitions)if(definition)
    if(const auto* result=std::get_if<OriginalResultDefinition>(&definition->origin);result&&result->operation==conversion) {
      producer=definition->input;mapped=true;
      Check(OriginalDefinitionKinds(*original,*definition).IsFiniteNumber());
    }
  Check(mapped);
  // The verified result belongs to the expression's normal completion, even
  // before a compiler temporary stores it. Recovery must consume this proof
  // for inline computations through the same current completion graph.
  Check(model.expressions.kinds[producer.value].IsFiniteNumber());
  const auto& interval=model.flow.original_results[conversion.value];
  Check(interval&&interval->expression==producer&&interval->entry==model.flow.expressions[producer.value]&&
      interval->normal==model.flow.normal_exits[producer.value]);
  const SemanticBudget exact{model.allocated_bytes,model.work};
  Check(std::holds_alternative<RecoveryProjection>(PrepareRecoveryProjection(tree,projection,input,exact)));
  Check(std::get<TreeStructureError>(PrepareRecoveryProjection(tree,projection,input,{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(PrepareRecoveryProjection(tree,projection,input,{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
  auto gap=model.flow;gap.original_results.clear();
  const auto gap_live=std::get<FlowLiveness>(AnalyzeFlowLiveness(gap));
  const auto gap_types=std::get<FlowNormalKinds>(AnalyzeFlowNormalKinds(tree,gap,gap_live,input));
  Check(gap_types.expressions[producer.value].IsNumber()&&!gap_types.expressions[producer.value].IsFiniteNumber());
  auto no_expression_boundaries=model.flow;no_expression_boundaries.expressions.clear();
  const auto no_boundaries_result=AnalyzeFlowNormalKinds(tree,no_expression_boundaries,gap_live,input);
  Check(std::holds_alternative<FlowNormalKinds>(no_boundaries_result));
  Check(!std::get<FlowNormalKinds>(no_boundaries_result).expressions[producer.value].IsFiniteNumber());
  // An actual producer edit invalidates the checked successful-definition
  // domain, even though the write, consumer and original correspondence remain present.
  auto changed_value=plan.transports;
  changed_value[producer.value]=ExpressionTransport{producer,PrimitiveId{0}};
  auto changed_plan=BindingPlanView{plan};changed_plan.transports=changed_value;
  RecoveryExpressionInput candidate{changed_plan,equations,order,original};
  const auto changed_result=PrepareRecoveryProjection(tree,projection,candidate);
  Check(std::holds_alternative<RecoveryProjection>(changed_result));
  const auto changed=std::get<RecoveryProjection>(changed_result);
  for(const auto site:observed)Check(changed.expressions.kinds[site.value]==ValueKinds::Boolean());
  auto transported_gap=changed.flow;transported_gap.expressions.clear();
  const auto transported_live=std::get<FlowLiveness>(AnalyzeFlowLiveness(transported_gap));
  const auto transported_types=AnalyzeFlowNormalKinds(tree,transported_gap,transported_live,candidate);
  Check(std::holds_alternative<FlowNormalKinds>(transported_types));
  for(const auto site:observed)Check(std::get<FlowNormalKinds>(transported_types).expressions[site.value]==ValueKinds::Boolean());
  if(branch) {
    auto transports=plan.transports;
    const auto condition=*original->operations[branch->value].expression;
    transports[condition.value]=ExpressionTransport{condition,PrimitiveId{0}};
    auto path_plan=BindingPlanView{plan};path_plan.transports=transports;
    RecoveryExpressionInput changed_path{path_plan,equations,order,original};
    const auto widened_result=PrepareRecoveryProjection(tree,projection,changed_path);
    Check(std::holds_alternative<RecoveryProjection>(widened_result));
    const auto widened=std::get<RecoveryProjection>(widened_result);
    for(const auto site:observed) {
      Check(widened.expressions.kinds[site.value].IsNumber());
      Check(!widened.expressions.kinds[site.value].IsFiniteNumber());
    }
    // Completion provenance belongs to the adapted graph. A consumer that
    // retains canonical value equations must not revive a path certificate
    // by failing to repeat the candidate's predicate-edit classification.
    const auto current_live=std::get<FlowLiveness>(AnalyzeFlowLiveness(widened.flow));
    const auto current_path=std::get<FlowNormalKinds>(AnalyzeFlowNormalKinds(tree,widened.flow,current_live,input));
    for(const auto site:observed) {
      Check(current_path.expressions[site.value].IsNumber());
      Check(!current_path.expressions[site.value].IsFiniteNumber());
    }
  }
  if(emit) {
    std::ostringstream output;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output,{}, {},nullptr,original)));
    std::cout<<nlohmann::json{{"source",output.str()},{"conditional",conditional},{"copies",copies},{"reverse",reverse},{"split",split},
      {"work",model.work},{"allocated_bytes",model.allocated_bytes},{"arena",arena},{"action",action}}<<'\n';
  }
  auto without_definitions=*original;
  for(auto& definition:without_definitions.definitions)if(definition)
    if(const auto* result=std::get_if<OriginalResultDefinition>(&definition->origin);result&&result->operation==conversion)definition.reset();
  RecoveryExpressionInput missing{plan,equations,order,&without_definitions};
  const auto inline_result=PrepareRecoveryProjection(tree,projection,missing);
  Check(std::holds_alternative<RecoveryProjection>(inline_result));
  // Losing a source-store certificate does not erase an independently checked
  // inline result. Losing both leaves only the current language semantics.
  for(const auto site:observed)Check(std::get<RecoveryProjection>(inline_result).expressions.kinds[site.value].IsFiniteNumber());
  for(auto& operation:without_definitions.operations)operation.result.reset();
  const auto conservative_result=PrepareRecoveryProjection(tree,projection,missing);
  Check(std::holds_alternative<RecoveryProjection>(conservative_result));
  const auto conservative=std::get<RecoveryProjection>(conservative_result);
  for(const auto site:observed) {
    Check(conservative.expressions.kinds[site.value].IsNumber());
    Check(!conservative.expressions.kinds[site.value].IsFiniteNumber());
  }
}

void OriginalCatchValues(bool emit=false,unsigned form=0,unsigned copies=0,bool split=false,bool reallocated=false) {
  using namespace analysis;
  const auto op=[](abc::Opcode value){return static_cast<std::uint8_t>(value);};
  std::vector<std::uint8_t> bytes;
  const auto saved=static_cast<std::uint8_t>(reallocated?2:0);
  const auto scratch=static_cast<std::uint8_t>(saved+1);
  const auto caught_slot=static_cast<std::uint8_t>(form==4?saved+2:saved);
  const unsigned locals=caught_slot+2;
  if(form==4)bytes={op(abc::Opcode::kLDAI_IMM32),7,0,0,0,op(abc::Opcode::kSTA_V8),saved};
  const auto protected_start=static_cast<unsigned>(bytes.size());
  if(form==0)bytes={op(abc::Opcode::kLDAI_IMM32),7,0,0,0};
  else if(form==1)bytes={op(abc::Opcode::kLDTRUE)};
  else if(form==2)bytes={op(abc::Opcode::kLDA_V8),static_cast<std::uint8_t>(locals)};
  else {
    bytes.insert(bytes.end(),{op(abc::Opcode::kLDA_V8),static_cast<std::uint8_t>(locals),op(abc::Opcode::kTONUMBER_IMM8),0});
    if(form==4)bytes.insert(bytes.end(),{op(abc::Opcode::kSTA_V8),saved,op(abc::Opcode::kLDA_V8),saved});
    else bytes.insert(bytes.end(),{op(abc::Opcode::kLDAI_IMM32),7,0,0,0});
  }
  for(unsigned i=0;i<copies;++i)
    bytes.insert(bytes.end(),{op(abc::Opcode::kSTA_V8),scratch,op(abc::Opcode::kLDA_V8),scratch});
  if(split) {
    constexpr std::uint8_t kJumpWidth=2; // Pinned JMP_IMM8 opcode plus displacement.
    bytes.insert(bytes.end(),{op(abc::Opcode::kJMP_IMM8),kJumpWidth});
  }
  constexpr unsigned kByteBits=8; // Pinned ISA prefix/subopcode byte encoding.
  const auto throwing=static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  bytes.push_back(static_cast<std::uint8_t>(throwing));
  bytes.push_back(static_cast<std::uint8_t>(throwing>>kByteBits));
  const auto handler=static_cast<unsigned>(bytes.size());
  bytes.insert(bytes.end(),{op(abc::Opcode::kSTA_V8),caught_slot,op(abc::Opcode::kNEG_IMM8),0,
      op(abc::Opcode::kLDA_V8),saved,op(abc::Opcode::kRETURN)});
  auto body=test::Body(bytes);
  abc::TryRegion region;region.offset={protected_start};region.length=handler-protected_start;region.handler_count=1;body.tries.push_back(region);
  abc::CatchHandler caught;caught.offset={handler};caught.length=bytes.size()-handler;body.handlers.push_back(caught);
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(locals,1,{}));
  const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,abc::ProfileId::kDynamic1206,frame.registers));
  IdfQuota ssa_work{kDefaultSemanticWork};
  const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,frame.registers,ssa_work));
  const auto env=std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  std::vector<EntryValueFact> entries;
  for(unsigned i=0;i<ssa.Values().size();++i)
    if(std::holds_alternative<MethodEntry>(ssa.Values()[i].origin))entries.push_back({{i},ValueKinds::Language()});
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values=std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,entries,value_work,{},
      PendingExceptionValues::kLanguage,{},{},CallResultValues::kLanguage,FrameReceiver::kUnknown,
      Arithmetic(SourceContract::kLanguageNumericV1)));
  const auto proof=std::get<ExceptionInputProof>(VerifyExceptionInputs(flow,ssa,env,values));
  const PhasedValueInput input{flow,ssa,env,values,proof,frame};
  const auto graph=std::get<ValueProgram>(ImportPhasedValues(input));
  const auto evaluation=std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
  const auto regions=std::get<ExceptionPlan>(BuildExceptionPlan(body,graph,input));
  const auto control=std::get<ExceptionControl>(BuildExceptionControl(body,graph,input,regions));
  const ExceptionTreeInput context{body,input,regions,control};
  FunctionSyntax syntax{};syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;
  syntax.identity.contract=SourceContract::kLanguageNumericV1;syntax.parameters.resize(1);
  std::vector<ExpressionConstant> constants(graph.ops.size());
  const auto draft=std::get<ExceptionFunctionDraft>(BuildExceptionFunctionDraft(graph,context,evaluation,syntax,{constants,0}));
  const auto& tree=draft.tree;const auto& original=*draft.flow->original;
  const auto recovered=std::get<SourceVariables>(PrepareSourceVariables(tree,{},&original));
  const auto& p=recovered.bindings;
  std::vector<FlowValueEquation> eq(tree.expressions.size());std::vector<ExprId> ids;
  for(unsigned i=0;i<eq.size();++i) {ids.push_back({i});if(const auto* r=std::get_if<SymbolExpr>(&tree.expressions[i].expression))eq[i]=FlowBindingValue{r->symbol};}
  std::vector<std::uint8_t> priv(tree.symbols.size());for(unsigned i=0;i<priv.size();++i)priv[i]=tree.symbols[i].kind==BindingKind::kCompilerTemporary;
  LegacyFlowProjection lp{p.aliases,p.literals,p.replacements,p.omitted,{},{},p.statements,priv,p.storage,p.undefined_stores,p.declaration_values,p.declaration_completions};
  RecoveryExpressionInput ri{p,eq,ids,&original};
  const auto m=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,lp,ri));
  const SemanticBudget exact{m.allocated_bytes,m.work};
  Check(std::holds_alternative<RecoveryProjection>(PrepareRecoveryProjection(tree,lp,ri,exact)));
  Check(std::get<TreeStructureError>(PrepareRecoveryProjection(tree,lp,ri,{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(PrepareRecoveryProjection(tree,lp,ri,{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
  bool original_catch=false;
  for(const auto& d:m.flow.definitions)if(d.kind==FlowDefinitionKind::kCatch) {
    Check(d.original_catch&&d.catch_entry);original_catch=true;
    Check(m.flow.bindings[d.binding.value].exposure==FlowBindingExposure::kObservable);
  }
  Check(original_catch);
  if(form==0) {
    // A full candidate changes the pending value. Reusing the raw numeric
    // Catch/result domain would silently override this current Boolean input.
    std::optional<ExprId> thrown;
    for(const auto& event:m.flow.events)if(event.thrown_value)thrown=event.thrown_value;
    Check(thrown.has_value());
    auto primitives=p.primitives;const PrimitiveId boolean{static_cast<unsigned>(primitives.size())};
    primitives.push_back(BooleanLiteral{true});
    auto transports=p.transports;transports[thrown->value]=ExpressionTransport{*thrown,boolean};
    auto candidate=BindingPlanView{p};candidate.primitives=primitives;candidate.transports=transports;
    const RecoveryExpressionInput changed_input{candidate,eq,ids,&original};
    const auto changed=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,lp,changed_input));
    bool read_seen=false;
    for(const auto& reference:changed.flow.references)if(reference.kind==FlowReferenceKind::kRead) {
      const auto* expression=std::get_if<ExprId>(&reference.site);
      if(expression&&tree.symbols[reference.binding.value].kind==BindingKind::kCatchParameter) {
        Check(changed.expressions.kinds[expression->value]==ValueKinds::Boolean());read_seen=true;
      }
    }
    Check(read_seen);
    auto without_catch=original;without_catch.catches.clear();
    const RecoveryExpressionInput source_input{p,eq,ids,&without_catch};
    const auto source_only=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,lp,source_input));
    for(const auto& reference:source_only.flow.references)if(reference.kind==FlowReferenceKind::kRead) {
      const auto* expression=std::get_if<ExprId>(&reference.site);
      if(expression&&tree.symbols[reference.binding.value].kind==BindingKind::kCatchParameter)
        Check(source_only.expressions.kinds[expression->value].IsNumber());
    }
  }
  bool observed=false;
  for(unsigned i=0;i<tree.expressions.size();++i)if(const auto* unary=std::get_if<UnaryExpr>(&tree.expressions[i].expression);unary&&unary->operation==TreeUnary::kNegate) {
    observed=true;
    if(form>=2)Check(m.flow.expression_owners[i]&&m.expressions.evaluations[i]==ExpressionEvaluation::kRequired);
    else Check(!m.flow.expression_owners[i]);
  }
  Check(observed);
  if(emit) {
    std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output,{}, {},nullptr,&original)));
    std::cout<<nlohmann::json{{"source",output.str()},{"form",form},{"copies",copies},{"split",split},
      {"reallocated",reallocated},{"operations",graph.ops.size()},{"values",graph.values.size()},
      {"work",draft.work+m.work},{"bytes",draft.allocated_bytes+m.allocated_bytes}}<<'\n';
  }
}

void UnprovedTryRefusal() {
  Fixture fixture;
  fixture.Add(0, 0, 0, analysis::ValueReturn{}, false,
      {{analysis::OperandRole::kReturnValue, analysis::ValueId{0}}});
  fixture.graph.blocks = {{{0}, 1, {0}, 0}};
  auto tree = std::get<ConstructedFunction>(fixture.Build()).tree;
  Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
  const auto original = std::get<BlockStmt>(tree.statements[tree.body.value].statement);
  const auto origin = tree.statements[tree.body.value].origin;
  const auto scope = static_cast<std::uint32_t>(tree.scopes.size());
  const auto statement = static_cast<std::uint32_t>(tree.statements.size());
  const auto child = static_cast<std::uint32_t>(tree.children.size());
  tree.scopes.push_back({ScopeKind::kBlock, tree.body_scope});
  tree.scopes.push_back({ScopeKind::kBlock, tree.body_scope});
  tree.statements.push_back({TryStmt{{statement + 1}, {}, StmtId{statement + 2}}, origin});
  tree.statements.push_back({BlockStmt{{scope}, original.first_child, original.child_count}, origin});
  tree.statements.push_back({BlockStmt{{scope + 1}, child, 0}, origin});
  tree.children.push_back({statement});
  tree.statements[tree.body.value].statement = BlockStmt{tree.body_scope, child, 1};
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  // Legal syntax is not authorization to add a handler/completion region to
  // normal IR. The common semantic publication gate must still reject it.
  Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(tree)));
}
constexpr auto kRead = analysis::LanguageEffect::kHeapRead | analysis::LanguageEffect::kUserCode;
constexpr auto kWrite = analysis::LanguageEffect::kHeapWrite | analysis::LanguageEffect::kUserCode;

void ThrowTree() {
  using namespace analysis;
  Fixture fixture;
  fixture.Add(0, 0, 2, ValueThrow{}, false, {{OperandRole::kThrownValue, ValueId{0}}});
  fixture.graph.ops[0].exceptional = ExceptionalCompletion::kAlwaysThrow;
  fixture.graph.blocks = {{{0}, 1, {0}, 0}};
  auto built = fixture.Build();
  Check(std::holds_alternative<ConstructedFunction>(built));
  auto tree = std::get<ConstructedFunction>(built).tree;
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
  const auto statement = std::get<StmtId>(tree.operations.front().node);
  const auto expression = std::get<ThrowStmt>(tree.statements[statement.value].statement).value;
  Check(std::get<OriginalSite>(tree.origins[tree.statements[statement.value].origin.value]).pc ==
      abc::CodeOffset{2});
  tree.statements[statement.value].statement = ReturnStmt{expression};
  Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(tree)));
  tree.statements[statement.value].statement = ExpressionStmt{expression};
  Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(tree)));
  tree.statements[statement.value].statement = ThrowStmt{ExprId{UINT32_MAX}};
  Check(std::holds_alternative<TreeStructureError>(VerifyTreeStructure(tree)));
}

Fixture BranchFixture() {
  using namespace analysis;
  Fixture f;
  f.Add(0, 0, 14, ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{0}}}, kRead);
  f.Add(0, 1, 21, ValueBinary{BinaryOperator::kStrictEqual}, true,
        {{OperandRole::kLeft, ValueId{2}}, {OperandRole::kRight, SpecialValue::kUndefined}});
  f.Add(0, 2, 24, ValueBranch{UnaryOperator::kTrueOrNonzeroNumber}, false,
        {{OperandRole::kCondition, ValueId{3}}});
  f.Add(1, 0, 36, ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{0}}}, kRead);
  f.Add(1, 1, 40, ValueNamedWrite{{0}}, false,
        {{OperandRole::kBase, ValueId{1}}, {OperandRole::kStoredValue, ValueId{4}}}, kWrite);
  f.Add(1, 2, 44, ValueJump{}, false, {});
  f.Add(2, 0, 46, ValueReturn{}, false, {{OperandRole::kReturnValue, SpecialValue::kUndefined}});
  f.graph.blocks = {{{0}, 3, {0}, 2}, {{3}, 3, {2}, 1}, {{6}, 1, {3}, 0}};
  f.graph.edges = {{{0}, {2}, FlowEdgeKind::kTaken, 0},
                   {{0}, {1}, FlowEdgeKind::kFallthrough, 1},
                   {{1}, {2}, FlowEdgeKind::kTaken, 0}};
  return f;
}
void EmptyTakenBranch() {
  auto fixture = BranchFixture();
  const auto built = std::get<ConstructedFunction>(fixture.Build());
  const auto& tree = built.tree;
  const auto& body = std::get<BlockStmt>(tree.statements[tree.body.value].statement);
  const auto& branch = std::get<IfStmt>(tree.statements[tree.children[body.first_child].value].statement);
  Check(!branch.alternate);
  Check(std::get<BlockStmt>(tree.statements[branch.consequent.value].statement).child_count == 1);
  Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
  auto wrong = tree;
  std::get<UnaryExpr>(wrong.expressions[branch.condition.value].expression).operation = TreeUnary::kPlus;
  Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(wrong)));
}
void SharedConditionTree() {
  using namespace analysis;
  Fixture f;
  for (std::uint32_t b = 0; b < 2; ++b) {
    const auto read = f.Add(b, 0, b * 10, ValueNamedRead{{0}}, true,
        {{OperandRole::kBase, ValueId{0}}}, kRead);
    const auto compare = f.Add(b, 1, b * 10 + 1, ValueBinary{BinaryOperator::kStrictEqual}, true,
        {{OperandRole::kLeft, *f.graph.ops[read.value].result}, {OperandRole::kRight, SpecialValue::kUndefined}});
    f.Add(b, 2, b * 10 + 2, ValueBranch{UnaryOperator::kTrueOrNonzeroNumber}, false,
        {{OperandRole::kCondition, *f.graph.ops[compare.value].result}});
  }
  f.Add(2, 0, 30, ValueNamedWrite{{0}}, false,
      {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue, analysis::IntegerLiteral{7}}}, kWrite);
  f.Add(2, 1, 31, ValueJump{}, false, {});
  f.Add(3, 0, 40, ValueReturn{}, false, {{OperandRole::kReturnValue, SpecialValue::kUndefined}});
  f.graph.blocks = {{{0}, 3, {0}, 2}, {{3}, 3, {2}, 2}, {{6}, 2, {4}, 1}, {{8}, 1, {5}, 0}};
  f.graph.edges = {{{0}, {2}, FlowEdgeKind::kTaken, 0}, {{0}, {1}, FlowEdgeKind::kFallthrough, 1},
      {{1}, {3}, FlowEdgeKind::kTaken, 0}, {{1}, {2}, FlowEdgeKind::kFallthrough, 1},
      {{2}, {3}, FlowEdgeKind::kFallthrough, 0}};
  for (bool head_taken : {false, true}) for (bool child_taken : {false, true}) {
    auto oriented = f;
    if (head_taken) std::swap(oriented.graph.edges[0].target, oriented.graph.edges[1].target);
    if (child_taken) std::swap(oriented.graph.edges[2].target, oriented.graph.edges[3].target);
    auto tree = std::get<ConstructedFunction>(oriented.Build()).tree;
    Check(std::holds_alternative<TreeSemantics>(oriented.Verify(tree)));
    const auto& body = std::get<BlockStmt>(tree.statements[tree.body.value].statement);
    const auto& branch = std::get<IfStmt>(tree.statements[tree.children[body.first_child].value].statement);
    const auto logical = std::get<LogicalExpr>(tree.expressions[branch.condition.value].expression);
    Check(logical.operation == TreeLogical::kOr && !branch.alternate);
    tree.expressions[branch.condition.value].expression = LogicalExpr{TreeLogical::kAnd, logical.left, logical.right};
    Check(std::holds_alternative<TreeSemanticError>(oriented.Verify(tree)));
  }
}
void BranchTree() {
  auto f = BranchFixture();
  auto built = f.Build();
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  Check(tree.symbols.size() == 1 && tree.parameters.size() == 1);
  Check(tree.names[tree.symbols[0].name.value] == u"params");
  std::size_t reads = 0;
  for (const auto& node : tree.expressions)
    if (std::holds_alternative<MemberExpr>(node.expression)) ++reads;
  Check(reads == 2);
  const auto& root = std::get<BlockStmt>(tree.statements[tree.body.value].statement);
  Check(root.child_count == 2);
  const auto& branch = std::get<IfStmt>(tree.statements[tree.children[root.first_child].value].statement);
  Check(!branch.alternate);
  const auto& body = std::get<BlockStmt>(tree.statements[branch.consequent.value].statement);
  Check(body.child_count == 1);
  const auto& write = std::get<ExpressionStmt>(tree.statements[tree.children[body.first_child].value].statement);
  Check(std::holds_alternative<AssignmentExpr>(tree.expressions[write.expression.value].expression));
  Check(std::get<OriginalSite>(tree.origins[tree.expressions[write.expression.value].origin.value]).pc.value == 40);
  Check(tree.anchors.size() == 1);
  const auto& anchor = std::get<BlockBoundary>(tree.anchors[0].position);
  Check(anchor.block == branch.consequent && anchor.index == body.child_count);
  Check(tree.operations.size() == f.graph.ops.size());
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 4, abc::DecodeCallFlags({})));
  const auto census = BuildFunctionAbi(f.graph, tree, frame, 1);
  Check(std::holds_alternative<ConstructedAbi>(census));
  const auto& abi = std::get<ConstructedAbi>(census);
  Check(abi.abi.parameters.size() == 1 && abi.abi.length == 1);
  Check(abi.abi.parameters[0].reads.operands == 2);
  Check(abi.abi.parameters[0].reads.references == 2);
  Check(abi.abi.parameters[0].writes == 0);
  Check(abi.abi.receiver.operands == 1 && abi.abi.receiver.references == 1);
  auto callable_tree = tree;
  callable_tree.callable = CallableKind::kFunction;
  Check(std::holds_alternative<ConstructedAbi>(BuildFunctionAbi(f.graph, callable_tree, frame, 1)));
  callable_tree.callable = CallableKind::kArrow;
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(f.graph, callable_tree, frame, 1)) == FunctionAbiError::kReceiver);
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(f.graph, tree, frame, 2)) == FunctionAbiError::kLength);
  // Census is not a source-publication proof: these mutations exercise only
  // signature shape/length. Default body correspondence is verified separately.
  auto default_tree = tree;
  const ExprId initializer{static_cast<std::uint32_t>(default_tree.expressions.size())};
  default_tree.expressions.push_back({TreeLiteral{UndefinedLiteral{}}, {0}});
  default_tree.parameters[0].initializer = initializer;
  const auto default_census = BuildFunctionAbi(f.graph, default_tree, frame, 0);
  Check(std::holds_alternative<ConstructedAbi>(default_census));
  Check(std::get<ConstructedAbi>(default_census).abi.layout == ParameterLayout::kLiteralDefaults);
  Check(std::get<ConstructedAbi>(default_census).abi.length == 0);
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(f.graph, default_tree, frame, 1)) == FunctionAbiError::kLength);
  default_tree.expressions.back().expression = SymbolExpr{default_tree.parameters[0].symbol};
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(f.graph, default_tree, frame, 0)) == FunctionAbiError::kParameter);
  default_tree.expressions.back().expression = TreeLiteral{UndefinedLiteral{}};
  default_tree.parameters[0].rest = true;
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(f.graph, default_tree, frame, 0)) == FunctionAbiError::kParameter);
  auto changed_graph = f.graph;
  ++changed_graph.revision.value;
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(changed_graph, tree, frame, 1)) == FunctionAbiError::kRevision);
  changed_graph = f.graph;
  changed_graph.values[0].origin = analysis::EntryDefinition{analysis::CallableEntry{}};
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(changed_graph, tree, frame, 1)) == FunctionAbiError::kEntry);
  auto absent_receiver = frame;
  absent_receiver.this_value.reset();
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(f.graph, tree, absent_receiver, 1)) == FunctionAbiError::kReceiver);
  analysis::SemanticBudget census_budget;
  census_budget.work = abi.work - 1;
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(f.graph, tree, frame, 1, census_budget)) == FunctionAbiError::kWorkLimit);
  ++census_budget.work;
  Check(std::holds_alternative<ConstructedAbi>(BuildFunctionAbi(f.graph, tree, frame, 1, census_budget)));
  analysis::SemanticBudget limited;
  limited.work = std::get<ConstructedFunction>(built).work - 1;
  Check(std::get<FunctionTreeError>(f.Build(limited)) == FunctionTreeError::kWorkLimit);
  limited.work += 1;
  Check(std::holds_alternative<ConstructedFunction>(f.Build(limited)));
}
void SnapshotTree() {
  using namespace analysis;
  Fixture f;
  f.Add(0, 0, 0, ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{0}}}, kRead);
  f.Add(0, 1, 4, ValueBinary{BinaryOperator::kStrictEqual}, true,
        {{OperandRole::kLeft, ValueId{2}}, {OperandRole::kRight, ValueId{2}}});
  f.Add(0, 2, 8, ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{3}}});
  f.graph.blocks = {{{0}, 3, {0}, 0}};
  const auto built = f.Build();
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  Check(tree.symbols.size() == 2);
  Check(tree.symbols[1].temporary == TemporaryReason::kMultipleUses);
  const auto& root = std::get<BlockStmt>(tree.statements[tree.body.value].statement);
  Check(root.child_count == 2);
  const auto& declaration = std::get<DeclarationStmt>(tree.statements[tree.children[root.first_child].value].statement);
  Check(declaration.symbol == SymbolId{1} && !declaration.initializer);
  const auto& returned = std::get<ReturnStmt>(tree.statements[tree.children[root.first_child + 1].value].statement);
  const auto& comparison = std::get<BinaryExpr>(tree.expressions[returned.value->value].expression);
  const auto& assignment = std::get<AssignmentExpr>(tree.expressions[comparison.left.value].expression);
  Check(std::get<SymbolTarget>(assignment.target).symbol == SymbolId{1});
  Check(std::get<SymbolExpr>(tree.expressions[comparison.right.value].expression).symbol == SymbolId{1});
  Check(std::holds_alternative<TreeSemantics>(f.Verify(tree)));
}
void NameAndConstantRefusals() {
  auto f = BranchFixture();
  f.constants[0] = NameId{2};
  Check(std::get<FunctionTreeError>(f.Build()) == FunctionTreeError::kExpression);
  f.constants[0] = NameId{1};
  auto control = BuildControlPlan(f.graph);
  auto evaluation = BuildEvaluationPlan(f.graph);
  auto syntax = f.Syntax();
  syntax.names[0] = u"$value0";
  auto built = BuildFunctionTree(f.graph, std::get<ControlPlan>(control),
      std::get<EvaluationPlan>(evaluation), std::move(syntax), {f.constants, 2, 0, {}});
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(std::holds_alternative<TreeSemantics>(f.Verify(tree)));
  Check(tree.names[tree.symbols[tree.parameters[0].symbol.value].name.value] == u"$value0");
  std::vector<std::u16string_view> bindings;
  for (const auto& symbol : tree.symbols) bindings.push_back(tree.names[symbol.name.value]);
  std::sort(bindings.begin(), bindings.end());
  Check(std::adjacent_find(bindings.begin(), bindings.end()) == bindings.end());
  syntax = f.Syntax(); syntax.names[0] = u"eval";
  built = BuildFunctionTree(f.graph, std::get<ControlPlan>(control),
      std::get<EvaluationPlan>(evaluation), std::move(syntax), {f.constants, 2, 0, {}});
  Check(std::get<FunctionTreeError>(built) == FunctionTreeError::kName);
}
void PredicateSnapshot() {
  using namespace analysis;
  for (const auto predicate : {UnaryOperator::kFalseOrZero, UnaryOperator::kTrueOrNonzeroNumber}) {
    Fixture f;
    f.Add(0, 0, 0, ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{0}}}, kRead);
    f.Add(0, 1, 4, ValueUnary{predicate}, true, {{OperandRole::kInput, ValueId{2}}});
    f.Add(0, 2, 8, ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{3}}});
    f.graph.blocks = {{{0}, 3, {0}, 0}};
    f.graph.origins[0].original = abc::CodeOffset{91};
    auto control = BuildControlPlan(f.graph);
    auto evaluation = BuildEvaluationPlan(f.graph);
    auto syntax = f.Syntax();
    syntax.texts.push_back(u"number");
    const auto built = BuildFunctionTree(f.graph, std::get<ControlPlan>(control),
        std::get<EvaluationPlan>(evaluation), std::move(syntax), {f.constants, 2, 1, TextId{0}});
    Check(std::holds_alternative<ConstructedFunction>(built));
    const auto& tree = std::get<ConstructedFunction>(built).tree;
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
    Check(tree.symbols.size() == 2 && tree.symbols[1].temporary == TemporaryReason::kPredicateSnapshot);
    std::size_t reads = 0;
    std::size_t references = 0;
    for (const auto& node : tree.expressions) {
      if (std::holds_alternative<MemberExpr>(node.expression)) ++reads;
      if (const auto* symbol = std::get_if<SymbolExpr>(&node.expression))
        if (symbol->symbol == SymbolId{1}) ++references;
    }
    Check(reads == 1);
    Check(references == (predicate == UnaryOperator::kFalseOrZero ? 2U : 3U));
    Check(std::get<OriginalSite>(tree.origins[0]).original == abc::CodeOffset{91});
  }
}
void SemanticMutations() {
  auto fixture = BranchFixture();
  const auto built = fixture.Build();
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto original = std::get<ConstructedFunction>(built).tree;
  const auto checked = fixture.Verify(original);
  Check(std::holds_alternative<TreeSemantics>(checked));
  const auto& root = std::get<BlockStmt>(original.statements[original.body.value].statement);
  const auto branch_id = original.children[root.first_child];
  const auto& branch = std::get<IfStmt>(original.statements[branch_id.value].statement);
  const auto& alternative = std::get<BlockStmt>(original.statements[branch.consequent.value].statement);
  const auto write_id = original.children[alternative.first_child];
  const auto assignment_id = std::get<ExpressionStmt>(original.statements[write_id.value].statement).expression;
  const auto return_id = original.children[root.first_child + 1];
  const auto return_value = *std::get<ReturnStmt>(original.statements[return_id.value].statement).value;

  auto tree = original;
  auto& reversed = std::get<IfStmt>(tree.statements[branch_id.value].statement);
  reversed.alternate = reversed.consequent;
  Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(tree)));

  tree = original;
  std::get<BinaryExpr>(tree.expressions[std::get<UnaryExpr>(tree.expressions[branch.condition.value].expression).operand.value].expression).operation = analysis::BinaryOperator::kStrictNotEqual;
  Check(std::get<TreeSemanticError>(fixture.Verify(tree)) == TreeSemanticError::kOperator);

  tree = original;
  auto& assigned = std::get<AssignmentExpr>(tree.expressions[assignment_id.value].expression);
  std::swap(std::get<MemberTarget>(assigned.target).base, assigned.value);
  Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(tree)));

  tree = original;
  std::get<MemberTarget>(std::get<AssignmentExpr>(tree.expressions[assignment_id.value].expression).target).key = NameId{0};
  Check(std::get<TreeSemanticError>(fixture.Verify(tree)) == TreeSemanticError::kConstant);

  tree = original;
  tree.expressions[return_value.value].expression = TreeLiteral{BooleanLiteral{false}};
  Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(tree)));

  tree = original;
  std::get<OriginalSite>(tree.origins[4]).pc.value = 41;
  Check(std::get<TreeSemanticError>(fixture.Verify(tree)) == TreeSemanticError::kOrigin);

  tree = original;
  tree.expressions[branch.condition.value].origin = {4};
  Check(std::get<TreeSemanticError>(fixture.Verify(tree)) == TreeSemanticError::kOrigin);

  tree = original;
  ++tree.value_revision.value;
  Check(std::get<TreeSemanticError>(fixture.Verify(tree)) == TreeSemanticError::kFlow);

  analysis::SemanticBudget budget;
  budget.work = std::get<TreeSemantics>(checked).work - 1;
  Check(std::get<TreeSemanticError>(fixture.Verify(original, budget)) == TreeSemanticError::kWorkLimit);
  ++budget.work;
  Check(std::holds_alternative<TreeSemantics>(fixture.Verify(original, budget)));
}

enum class CallCase { kValue, kMember, kConstruct };
void GlobalTree() {
  using namespace analysis;
  Fixture fixture;
  fixture.Add(0, 0, 31, ValueGlobalRead{GlobalLookup::kName, {0}}, true, {}, LanguageEffect::kGlobalAccess);
  fixture.Add(0, 1, 35, ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{2}}});
  fixture.graph.blocks = {{{0}, 2, {0}, 0}};
  auto built = fixture.Build();
  Check(std::holds_alternative<ConstructedFunction>(built));
  auto tree = std::get<ConstructedFunction>(std::move(built)).tree;
  Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
  const GlobalScope scope{};
  Check(std::holds_alternative<TreeGlobals>(VerifyTreeGlobals(tree, &scope)));
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, nullptr)) == TreeGlobalError::kMissingContext);
  bool changed = false;
  for (auto& node : tree.expressions) {
    if (auto* global = std::get_if<GlobalNameExpr>(&node.expression)) {
      global->name = {0}; changed = true;
    }
  }
  Check(changed);
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kShadowed);
  Check(std::get<TreeSemanticError>(fixture.Verify(tree)) == TreeSemanticError::kConstant);
  std::get<ValueGlobalRead>(fixture.graph.ops[0].operation).lookup = GlobalLookup::kVar;
  Check(std::get<FunctionTreeError>(fixture.Build()) == FunctionTreeError::kExpression);
}
void ConstructorGuardAnchors() {
  using namespace analysis;
  Fixture fixture;
  fixture.Add(0, 0, 0, ValueSuperCall{0, {}}, true, {}, LanguageEffect::kUserCode);
  fixture.Add(0, 1, 4, ValueSuperCheck{SuperCheckKind::kRequireUninitialized}, false,
      {{OperandRole::kGuardValue, ValueId{1}}}, LanguageEffect::kGuard);
  fixture.Add(0, 2, 8, ValueSuperCheck{SuperCheckKind::kRequireInitialized}, false,
      {{OperandRole::kGuardValue, ValueId{2}}}, LanguageEffect::kGuard);
  for (auto& use : fixture.graph.uses) use.purpose = ReadPurpose::kProtocol;
  fixture.Add(0, 3, 12, ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{2}}});
  fixture.graph.blocks = {{{0}, 4, {0}, 0}};
  const auto control = std::get<ControlPlan>(BuildControlPlan(fixture.graph));
  const std::array<OpId, 2> guards{{{1}, {2}}};
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(fixture.graph, {}, IncomingCells::kReject, {}, {}, {}, {}, ClosureWrites::kUnknown, {}, guards));
  auto missing = evaluation; missing.receiver_guards.pop_back();
  Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(fixture.graph, missing)));
  auto unprojected = evaluation; unprojected.operations[guards.front().value].kind = EvaluationKind::kRoot;
  Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(fixture.graph, unprojected)));
  auto syntax = fixture.Syntax(); syntax.callable = CallableKind::kDerivedConstructor;
  ExpressionConstants constants{fixture.constants, 2, 0, {}}; constants.receiver_guards = guards;
  const auto built = BuildFunctionTree(fixture.graph, control, evaluation, syntax, constants);
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(tree.receiver_guards.size() == guards.size());
  Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(fixture.graph, control, evaluation, tree, constants)));
  for (const auto& expression : tree.expressions) Check(!std::holds_alternative<ThisExpr>(expression.expression));
  constants.receiver_guards = {};
  Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(fixture.graph, control, evaluation, tree, constants)));
  Check(std::holds_alternative<FunctionTreeError>(BuildFunctionTree(fixture.graph, control, evaluation, syntax, constants)));
}
void DerivedEntryValue() {
  using namespace analysis;
  Fixture fixture;
  fixture.Add(0, 0, 0, ValueInvoke{InvokeReceiver::kUndefined, 1}, true,
      {{OperandRole::kCallee, ValueId{0}}, {OperandRole::kArgument, ValueId{1}}}, LanguageEffect::kUserCode);
  fixture.Add(0, 1, 4, ValueSuperCall{0, {}}, true, {}, LanguageEffect::kUserCode);
  fixture.Add(0, 2, 8, ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{3}}});
  fixture.graph.blocks = {{{0}, 3, {0}, 0}};
  const auto control = std::get<ControlPlan>(BuildControlPlan(fixture.graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(fixture.graph));
  auto syntax = fixture.Syntax(); syntax.callable = CallableKind::kDerivedConstructor;
  auto tree = std::get<ConstructedFunction>(BuildFunctionTree(fixture.graph, control, evaluation,
      syntax, {fixture.constants, 2, 0, {}})).tree;
  Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 4, abc::DecodeCallFlags({})));
  Check(std::holds_alternative<ConstructedAbi>(BuildFunctionAbi(fixture.graph, tree, frame, 1)));
  bool changed = false;
  for (auto& node : tree.expressions) if (const auto* literal = std::get_if<TreeLiteral>(&node.expression))
    if (std::holds_alternative<UndefinedLiteral>(*literal)) { node.expression = ThisExpr{tree.owner}; changed = true; break; }
  Check(changed && std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(tree)));
  // ABI requires that correspondence check. Its census cannot distinguish an
  // illegal entry read from a legal initialized-super result by read counts.
  auto wrong_owner = tree;
  for (auto& node : wrong_owner.expressions)
    if (auto* receiver = std::get_if<ThisExpr>(&node.expression)) receiver->owner.value += 1;
  Check(std::get<FunctionAbiError>(BuildFunctionAbi(fixture.graph, wrong_owner, frame, 1)) == FunctionAbiError::kReceiver);
}
void SuperTree() {
  using namespace analysis;
  for (const unsigned count : {0U, 1U, 200U, 201U, 402U}) {
    Fixture fixture;
    std::vector<std::pair<OperandRole, ValueOperand>> inputs;
    for (unsigned i = 0; i < count; ++i)
      inputs.emplace_back(OperandRole::kArgument, analysis::IntegerLiteral{static_cast<std::int32_t>(i)});
    fixture.Add(0, 0, 0, ValueSuperCall{static_cast<std::uint16_t>(count), {}}, true, inputs, LanguageEffect::kUserCode);
    fixture.Add(0, 1, 4, ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{2}}});
    fixture.graph.blocks = {{{0}, 2, {0}, 0}};
    auto syntax = fixture.Syntax(); syntax.callable = CallableKind::kDerivedConstructor;
    const auto control = std::get<ControlPlan>(BuildControlPlan(fixture.graph));
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(fixture.graph));
    const auto built = BuildFunctionTree(fixture.graph, control, evaluation, syntax, {fixture.constants, 2, 0, {}});
    Check(std::holds_alternative<ConstructedFunction>(built));
    auto tree = std::get<ConstructedFunction>(built).tree;
    Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
    Check(tree.arguments.size() == count);
    if (count > 1) {
      std::swap(tree.arguments.front(), tree.arguments.back());
      Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
      Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(tree)));
      std::swap(tree.arguments.front(), tree.arguments.back());
    }
    bool found = false;
    for (auto& node : tree.expressions) {
      if (auto* call = std::get_if<SuperCallExpr>(&node.expression)) {
        Check(std::get<ArgumentRange>(call->arguments).count == count); found = true;
        ++std::get<ArgumentRange>(call->arguments).count;
      }
    }
    Check(found && std::holds_alternative<TreeStructureError>(VerifyTreeStructure(tree)));
  }
}
void CallTree(CallCase kind, std::uint32_t count, bool computed) {
  using namespace analysis;
  Fixture fixture;
  if (computed)
    fixture.Add(0, 0, 0, ValuePropertyRead{}, true,
        {{OperandRole::kBase, ValueId{1}}, {OperandRole::kKey, ValueId{0}}}, kRead);
  else fixture.Add(0, 0, 0, ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{1}}}, kRead);
  std::vector<std::pair<OperandRole, ValueOperand>> inputs;
  inputs.emplace_back(kind == CallCase::kConstruct ? OperandRole::kConstructor : OperandRole::kCallee, ValueId{2});
  if (kind == CallCase::kMember) inputs.emplace_back(OperandRole::kReceiver, ValueId{1});
  for (std::uint32_t i = 0; i < count; ++i)
    inputs.emplace_back(OperandRole::kArgument, analysis::IntegerLiteral{static_cast<std::int32_t>(i)});
  const ValueOperation operation = kind == CallCase::kConstruct ? ValueOperation{ValueConstruct{count}} :
      ValueOperation{ValueInvoke{kind == CallCase::kMember ? InvokeReceiver::kExplicit : InvokeReceiver::kUndefined, count}};
  fixture.Add(0, 1, 4, operation, true, inputs, LanguageEffect::kUserCode);
  fixture.Add(0, 2, 8, ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{3}}});
  fixture.graph.blocks = {{{0}, 3, {0}, 0}};
  const auto built = fixture.Build();
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(tree.arguments.size() == count);
  Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
  std::optional<ExprId> call;
  for (std::uint32_t i = 0; i < tree.expressions.size(); ++i)
    if (std::holds_alternative<CallExpr>(tree.expressions[i].expression) ||
        std::holds_alternative<ConstructExpr>(tree.expressions[i].expression)) call = ExprId{i};
  Check(call.has_value());
  if (kind != CallCase::kConstruct) {
    auto forged = tree;
    auto& node = std::get<CallExpr>(forged.expressions[call->value].expression);
    node.reference = kind == CallCase::kMember ? CallReference::kValue : CallReference::kMember;
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(forged)));
    Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(forged)));
  }
  if (count >= 2) {
    auto forged = tree;
    std::swap(forged.arguments[0], forged.arguments[1]);
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(forged)));
    Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(forged)));
    forged = tree;
    forged.arguments[1] = forged.arguments[0];
    Check(std::get<TreeStructureError>(VerifyTreeStructure(forged)) == TreeStructureError::kOwnership);
  }
  auto orphan = tree;
  orphan.arguments.push_back(*call);
  Check(std::get<TreeStructureError>(VerifyTreeStructure(orphan)) == TreeStructureError::kOwnership);
  auto invalid = tree;
  std::visit([&](auto& node) {
    using T = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<T, CallExpr>) ++node.arguments.count;
    else if constexpr (std::is_same_v<T, ConstructExpr>) ++std::get<ArgumentRange>(node.arguments).count;
  }, invalid.expressions[call->value].expression);
  Check(std::get<TreeStructureError>(VerifyTreeStructure(invalid)) == TreeStructureError::kRange);
}
void ComputedStoreTree() {
  using namespace analysis;
  Fixture fixture;
  fixture.Add(0, 0, 0, ValueNamedRead{{0}}, true,
      {{OperandRole::kBase, ValueId{0}}}, kRead);
  fixture.Add(0, 1, 4, ValuePropertyWrite{}, false,
      {{OperandRole::kBase, ValueId{1}}, {OperandRole::kKey, ValueId{0}},
       {OperandRole::kStoredValue, ValueId{2}}}, kWrite);
  fixture.Add(0, 2, 8, ValueReturn{}, false,
      {{OperandRole::kReturnValue, ValueId{1}}});
  fixture.graph.blocks = {{{0}, 3, {0}, 0}};
  const auto built = fixture.Build();
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
  const auto& original=*std::get<ConstructedFunction>(built).flow->original;
  const auto prepared=std::get<SourceVariables>(PrepareSourceVariables(tree));
  const auto& bindings=prepared.bindings;
  LegacyFlowProjection projection{bindings.aliases,bindings.literals,bindings.replacements,
      bindings.omitted,bindings.evaluations,bindings.normal_stores,bindings.statements,{},
      bindings.storage,bindings.undefined_stores,bindings.declaration_values,bindings.declaration_completions};
  projection.original=&original;
  const auto flow=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  bool original_write=false;
  for(const auto& property:flow.properties)if(property.kind==FlowReferenceKind::kWrite) {
    Check(property.original_base&&property.original_key);
    Check(flow.normal_exits[property.site.value]==property.normal);
    for(const auto pair:{std::pair{*property.original_base,OperandRole::kBase},std::pair{*property.original_key,OperandRole::kKey}}) {
      bool found=false;
      for(std::size_t i=0;i<pair.first.count;++i) {
        const auto& input=original.inputs[pair.first.first+i];
        if(input.occurrence) {const auto& use=original.uses[input.occurrence->value];found|=use.owner==std::variant<OpId,ValueId>{OpId{1}}&&use.role==pair.second;}
      }
      Check(found);
    }
    original_write=true;
  }
  Check(original_write);
  bool checked = false;
  auto sloppy = tree;
  sloppy.strictness = Strictness::kSloppy;
  Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(sloppy)));
  for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
    const auto* assignment = std::get_if<AssignmentExpr>(&tree.expressions[i].expression);
    if (!assignment) continue;
    const auto* member = std::get_if<MemberTarget>(&assignment->target);
    if (!member) continue;
    Check(std::holds_alternative<ComputedKey>(member->key));
    auto forged = tree;
    auto& target = std::get<MemberTarget>(std::get<AssignmentExpr>(forged.expressions[i].expression).target);
    std::swap(target.base, std::get<ComputedKey>(target.key).expression);
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(forged)));
    Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(forged)));
    checked = true;
  }
  Check(checked);
}
void LoopTree() {
  auto fixture = BranchFixture();
  // The body retains the two effectful property operations, then takes the
  // original edge back to the header. The header read is evaluated each time.
  fixture.graph.edges[2].target = {0};
  const auto built = fixture.Build();
  const auto* constructed = std::get_if<ConstructedFunction>(&built);
  Check(constructed != nullptr);
  const auto& tree = constructed->tree;
  Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
  std::size_t loops = 0, breaks = 0, continues = 0;
  for (std::size_t i = 0; i < tree.statements.size(); ++i) {
    const auto& statement = tree.statements[i];
    loops += std::holds_alternative<LoopStmt>(statement.statement);
    breaks += std::holds_alternative<BreakStmt>(statement.statement);
    continues += std::holds_alternative<ContinueStmt>(statement.statement);
    if (const auto* transfer = std::get_if<ContinueStmt>(&statement.statement)) {
      auto changed = tree;
      changed.statements[i].statement = BreakStmt{transfer->target};
      Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(changed)));
      Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(changed)));
      changed = tree;
      changed.origins[statement.origin.value] = EdgeOrigin{{0}};
      Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(changed)));
    }
  }
  Check(loops == 1 && breaks == 1 && continues == 1);
  auto changed = tree;
  for (auto& origin : changed.origins)
    if (auto* loop = std::get_if<LoopOrigin>(&origin)) loop->header = {1};
  Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(changed)));
}
void FreshAggregates() {
  using namespace analysis;
  for (const bool array : {false, true}) {
    Fixture fixture;
    fixture.Add(0, 0, 0, array ? ValueOperation{ValueArrayAllocation{}} :
        ValueOperation{ValueObjectAllocation{}}, true, {}, LanguageEffect::kAllocation);
    if (array) {
      fixture.graph.ops[0].before_throw = LanguageEffect::kNone;
      fixture.graph.ops[0].exceptional = ExceptionalCompletion::kImpossible;
    }
    // Two references must observe the very same allocation, not two literals.
    fixture.Add(0, 1, 2, ValueNamedWrite{{0}}, false,
        {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue, ValueId{2}}}, kWrite);
    fixture.Add(0, 2, 5, ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{2}}});
    fixture.graph.blocks = {{{0}, 3, {0}, 0}};
    const auto built = fixture.Build();
    const auto* result = std::get_if<ConstructedFunction>(&built);
    Check(result && std::holds_alternative<TreeSemantics>(fixture.Verify(result->tree)));
    std::size_t allocations = 0;
    for (std::size_t i = 0; i < result->tree.expressions.size(); ++i) {
      const auto& expression = result->tree.expressions[i].expression;
      if (!std::holds_alternative<EmptyArrayExpr>(expression) &&
          !std::holds_alternative<EmptyObjectExpr>(expression)) continue;
      ++allocations;
      auto changed = result->tree;
      changed.expressions[i].expression = array ? TreeExpression{EmptyObjectExpr{}} : TreeExpression{EmptyArrayExpr{}};
      Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(changed)));
      Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(changed)));
    }
    Check(allocations == 1);
    const auto control = std::get<ControlPlan>(BuildControlPlan(fixture.graph));
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(fixture.graph));
    const auto flow = std::get<TreeFlow>(VerifyTreeFlow(fixture.graph, control, evaluation, result->tree));
    Check(flow.snapshots[0].has_value());
  }
}
void OwnDefinitionRefusal() {
  using namespace analysis;
  for (const auto kind : {OwnWriteKind::kStore, OwnWriteKind::kDefineField}) {
    Fixture fixture;
    constexpr std::uint16_t kCacheSlot = 0;
    fixture.Add(0, 0, 0, ValueOwnWrite{kind, {0}, kCacheSlot}, false,
        {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue, ValueId{1}}}, kWrite);
    fixture.Add(0, 1, 5, ValueReturn{}, false,
        {{OperandRole::kReturnValue, SpecialValue::kUndefined}});
    fixture.graph.blocks = {{{0}, 2, {0}, 0}};
    // A typed imported definition cannot acquire assignment semantics through
    // the ordinary property writer without a separate descriptor/identity proof.
    Check(std::get<EvaluationError>(BuildEvaluationPlan(fixture.graph)) ==
        EvaluationError::kUnsupportedOperation);
  }
}
void ReceiverFields() {
  using namespace analysis;
  for (const bool split : {false, true}) {
    Fixture fixture;
    fixture.Add(0, 0, 0, ValueFrameThis{}, true, {});
    if (split) fixture.Add(0, 1, 1, ValueJump{}, false, {});
    const auto field = fixture.Add(split ? 1 : 0, split ? 0 : 1, 2,
        ValueOwnWrite{OwnWriteKind::kDefineField, {0}, 0}, false,
        {{OperandRole::kBase, ValueId{2}}, {OperandRole::kStoredValue, analysis::IntegerLiteral{42}}}, kWrite);
    fixture.constants[field.value] = NameId{1};
    fixture.Add(split ? 1 : 0, split ? 1 : 2, 3, ValueReturn{}, false,
        {{OperandRole::kReturnValue, SpecialValue::kUndefined}});
    fixture.graph.blocks = split ? std::vector<WorkBlock>{{{0}, 2, {0}, 1}, {{2}, 2, {1}, 0}} :
        std::vector<WorkBlock>{{{0}, 3, {0}, 0}};
    if (split) fixture.graph.edges = {{{0}, {1}, FlowEdgeKind::kTaken, 0}};
    auto built = fixture.Build();
    Check(std::holds_alternative<ConstructedFunction>(built));
    auto tree = std::get<ConstructedFunction>(std::move(built)).tree;
    Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
    const auto expression = std::get<ExprId>(tree.operations[field.value].node);
    Check(std::holds_alternative<FieldDefinitionExpr>(tree.expressions[expression.value].expression));
    auto changed = tree;
    const auto receiver = std::get<FieldDefinitionExpr>(changed.expressions[expression.value].expression).receiver;
    const auto read = std::get<ExprId>(tree.operations[0].node);
    changed.expressions[read.value].expression = ThisExpr{{tree.owner.value + 1}};
    Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(changed)));
    changed = tree;
    changed.expressions[receiver.value].expression = SymbolExpr{tree.parameters[0].symbol};
    Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(changed)));
    changed = tree;
    changed.expressions[expression.value].expression = AssignmentExpr{
        MemberTarget{receiver, NameId{1}}, std::get<FieldDefinitionExpr>(tree.expressions[expression.value].expression).value};
    Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(changed)));
  }
}
void DebuggerTree() {
  using namespace analysis;
  Fixture fixture;
  fixture.Add(0, 0, 0, ValueDebugger{}, false, {});
  fixture.graph.ops[0].normal = LanguageEffect::kDebugger;
  fixture.Add(0, 1, 1, ValueReturn{}, false,
      {{OperandRole::kReturnValue, SpecialValue::kTrue}});
  fixture.graph.blocks = {{{0}, 2, {0}, 0}};
  auto built = fixture.Build();
  auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
  const auto statement = std::get<StmtId>(tree.operations[0].node);
  Check(std::holds_alternative<DebuggerStmt>(tree.statements[statement.value].statement));
  tree.statements[statement.value].statement = ReturnStmt{};
  Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(tree)));
}
void CreationSelfTree() {
  using namespace analysis;
  for (const auto kind : {CallableKind::kFunction, CallableKind::kArrow, CallableKind::kAsyncFunction,
                          CallableKind::kAsyncArrow, CallableKind::kGenerator}) {
    Fixture fixture;
    fixture.Add(0, 0, 0, ValueFrameFunction{}, true, {});
    fixture.Add(0, 1, 1, ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{2}}});
    fixture.graph.blocks = {{{0}, 2, {0}, 0}};
    auto syntax = fixture.Syntax();
    syntax.callable = kind;
    syntax.inferred_name = NameId{0}; // Runtime name equals the recovered parameter name.
    const auto control = std::get<ControlPlan>(BuildControlPlan(fixture.graph));
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(fixture.graph));
    const auto build = [&] {
      return BuildFunctionTree(fixture.graph, control, evaluation, syntax, {fixture.constants, 2, 0, {}});
    };
    const auto built = build();
    Check(std::holds_alternative<ConstructedFunction>(built));
    const auto& tree = std::get<ConstructedFunction>(built).tree;
    Check(tree.self && tree.self_form == SelfForm::kCreationBinding);
    const auto& self = tree.symbols[tree.self->value];
    Check(self.evidence == NameEvidence::kSynthesized && self.name != tree.inferred_name);
    Check(tree.symbols[tree.parameters[0].symbol.value].name == tree.inferred_name);
    Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {}));
    Check(std::get<ConstructedAbi>(BuildFunctionAbi(fixture.graph, tree, frame, 1)).abi.frame_function_reads == 1);
    const auto reject = [&](auto change) {
      auto copy = tree; change(copy);
      Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(copy)));
    };
    reject([](auto& copy) { copy.self_form = SelfForm::kNamedFunction; });
    reject([](auto& copy) { copy.symbols[copy.self->value].name = *copy.inferred_name; });
    reject([](auto& copy) { copy.symbols[copy.self->value].evidence = NameEvidence::kMetadata; });
    reject([](auto& copy) { copy.symbols[copy.self->value].declaration = DeclarationKind::kLet; });
    reject([](auto& copy) { copy.identity.contract = SourceContract::kLanguageAndHostV1; });
    reject([](auto& copy) { copy.inferred_name.reset(); });
    syntax.identity.contract = SourceContract::kLanguageAndHostV1;
    Check(std::holds_alternative<FunctionTreeError>(build()));
  }
}
void FrameFunctionTree() {
  using namespace analysis;
  Fixture fixture;
  fixture.Add(0, 0, 0, ValueFrameFunction{}, true, {});
  fixture.Add(0, 1, 1, ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{2}}});
  fixture.graph.blocks = {{{0}, 2, {0}, 0}};
  auto syntax = fixture.Syntax();
  syntax.callable = CallableKind::kFunction;
  syntax.inferred_name = NameId{1};
  const auto control = std::get<ControlPlan>(BuildControlPlan(fixture.graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(fixture.graph));
  auto built = BuildFunctionTree(fixture.graph, control, evaluation, syntax, {fixture.constants, 2, 0, {}});
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto tree = std::get<ConstructedFunction>(std::move(built)).tree;
  Check(tree.self.has_value());
  const auto& self = tree.symbols[tree.self->value];
  Check(self.kind == BindingKind::kFunctionSelf && self.declaration == DeclarationKind::kConst);
  Check(tree.scopes[tree.parameter_scope.value].parent == self.scope);
  Check(tree.scopes[self.scope.value].kind == ScopeKind::kFunctionSelf);
  Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {}));
  Check(!frame.function);
  const auto abi = std::get<ConstructedAbi>(BuildFunctionAbi(fixture.graph, tree, frame, 1)).abi;
  Check(abi.frame_function_reads == 1 && abi.self_references == 1 &&
      abi.callable == AbiObservation::kFrameIdentity);
  const auto expression = std::get<ExprId>(tree.operations.front().node);
  Check(std::get<SymbolExpr>(tree.expressions[expression.value].expression).symbol == tree.self);
  const auto reject = [&](auto change) {
    auto copy = tree;
    change(copy);
    Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(copy)));
  };
  reject([](auto& copy) { copy.self.reset(); });
  reject([](auto& copy) { copy.symbols[copy.self->value].declaration = DeclarationKind::kLet; });
  reject([](auto& copy) { copy.scopes[copy.symbols[copy.self->value].scope.value].parent = copy.body_scope; });
  reject([](auto& copy) { copy.symbols[copy.parameters[0].symbol.value].name = *copy.inferred_name; });
  reject([](auto& copy) { copy.names[copy.inferred_name->value] = u"eval"; });
  reject([&](auto& copy) { copy.expressions[expression.value].expression = SymbolExpr{copy.parameters[0].symbol}; });
  for (const auto kind : {CallableKind::kArrow, CallableKind::kMethod, CallableKind::kDerivedConstructor,
                         CallableKind::kAsyncMethod, CallableKind::kAsyncArrow})
    reject([&](auto& copy) { copy.callable = kind; });
  auto async = tree;
  async.callable = CallableKind::kAsyncFunction;
  Check(std::holds_alternative<TreeSemantics>(fixture.Verify(async)));
  Check(std::get<ConstructedAbi>(BuildFunctionAbi(fixture.graph, async, frame, 1)).abi.frame_function_reads == 1);
  auto global = tree;
  global.expressions[expression.value].expression = GlobalNameExpr{*tree.inferred_name};
  const GlobalScope scope{};
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(global, &scope)) == TreeGlobalError::kShadowed);
  syntax.inferred_name.reset();
  Check(std::holds_alternative<FunctionTreeError>(BuildFunctionTree(fixture.graph, control, evaluation,
      syntax, {fixture.constants, 2, 0, {}})));
}
// A source-native entry value needs no private save, whether the shared
// evaluator places it at an eager occurrence or across a split block/effect.
// Getter snapshots and mutable cells must remain independent (SnapshotTree,
// capture_tree_test). These tests inspect fresh correspondence, not just text.
void InvocationBindingTrees() {
  using namespace analysis;
  for (const bool rest : {false, true}) for (const bool split : {false, true})
      for (const bool getter : {false, true}) {
    Fixture fixture;
    fixture.Add(0, 0, 10, rest ? ValueOperation{ValueRestArguments{0}}
        : ValueOperation{ValueFrameFunction{}}, true, {});
    if (split) fixture.Add(0, 1, 19, ValueJump{}, false, {});
    const auto block = split ? 1u : 0u;
    auto order = split ? 0u : 1u;
    if (getter) fixture.Add(block, order++, 32, ValueNamedRead{{0}}, true,
        {{OperandRole::kBase, ValueId{rest ? 2u : 0u}}}, kRead);
    const auto comparison = fixture.Add(block, order++, 47, ValueBinary{BinaryOperator::kStrictEqual}, true,
        {{OperandRole::kLeft, ValueId{2}}, {OperandRole::kRight, ValueId{2}}});
    fixture.Add(block, order, 58, ValueReturn{}, false,
        {{OperandRole::kReturnValue, *fixture.graph.ops[comparison.value].result}});
    if (split) {
      fixture.graph.blocks = {{{0}, 2, {0}, 1}, {{2}, order + 1, {1}, 0}};
      fixture.graph.edges = {{{0}, {1}, FlowEdgeKind::kTaken, 0}};
    } else fixture.graph.blocks = {{{0}, order + 1, {0}, 0}};
    auto syntax = fixture.Syntax();
    syntax.callable = CallableKind::kFunction;
    if (rest) syntax.parameters[0].rest = true;
    else syntax.inferred_name = NameId{1};
    const auto control = std::get<ControlPlan>(BuildControlPlan(fixture.graph));
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(fixture.graph));
    Check(evaluation.operations[0].temporary.has_value());
    auto built = BuildFunctionTree(fixture.graph, control, evaluation, syntax, {fixture.constants, 2, 0, {}});
    Check(std::holds_alternative<ConstructedFunction>(built));
    const auto& tree = std::get<ConstructedFunction>(built).tree;
    Check(tree.symbols.size() == (rest ? 1 : 2));
    Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
    const auto source = std::get<ExprId>(tree.operations[0].node);
    const auto expected = rest ? tree.parameters[0].symbol : *tree.self;
    Check(std::get<SymbolExpr>(tree.expressions[source.value].expression).symbol == expected);
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {}));
    const auto abi = BuildFunctionAbi(fixture.graph, tree, frame, rest ? 0 : 1);
    Check(std::holds_alternative<ConstructedAbi>(abi));
    if (rest) {
      const auto& formal = std::get<ConstructedAbi>(abi).abi.parameters[0];
      Check(formal.reads.operands == 0 && formal.reads.references >= 2 && formal.writes == 0);
    }
    std::ostringstream text;
    const auto printed = PrintFunctionTree(tree, text);
    Check(std::holds_alternative<PrintedFunction>(printed));
    Check(text.str().find("$value") == std::string::npos);
    if (evaluation.operations[0].kind == EvaluationKind::kRoot) {
      const auto range = std::get<PrintedFunction>(printed).expressions[source.value];
      Check(range.begin == range.end);
    }
    auto wrong = tree;
    if (rest) wrong.parameters[0].rest = false;
    else wrong.expressions[source.value].expression = SymbolExpr{tree.parameters[0].symbol};
    Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(wrong)));
    if (rest) {
      wrong = tree;
      const auto binary = std::get<ExprId>(tree.operations[comparison.value].node);
      const auto right = std::get<BinaryExpr>(tree.expressions[binary.value].expression).right;
      const ExprId value{static_cast<std::uint32_t>(wrong.expressions.size())};
      wrong.expressions.push_back({TreeLiteral{BooleanLiteral{false}}, wrong.expressions[right.value].origin});
      wrong.expressions[right.value].expression = AssignmentExpr{SymbolTarget{expected}, value};
      Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(wrong)));
      Check(std::get<TreeSemanticError>(fixture.Verify(wrong)) == TreeSemanticError::kFlow);
    }
  }
}
void InvocationBranchBindings() {
  using namespace analysis;
  for (const bool rest : {false, true}) for (const bool copy : {false, true}) {
    Fixture fixture;
    fixture.Add(0, 0, 8, rest ? ValueOperation{ValueRestArguments{1}}
        : ValueOperation{ValueFrameFunction{}}, true, {});
    ValueId value{2}; auto order = 1u;
    if (copy) {
      const auto copied = fixture.Add(0, order++, 13, ValueCopy{}, true, {{OperandRole::kInput, value}});
      value = *fixture.graph.ops[copied.value].result;
    }
    const auto test = fixture.Add(0, order++, 29, ValueBinary{BinaryOperator::kStrictEqual}, true,
        {{OperandRole::kLeft, ValueId{0}}, {OperandRole::kRight, SpecialValue::kNull}});
    fixture.Add(0, order++, 37, ValueBranch{UnaryOperator::kTrueOrNonzeroNumber}, false,
        {{OperandRole::kCondition, *fixture.graph.ops[test.value].result}});
    const auto comparison = fixture.Add(1, 0, 54, ValueBinary{BinaryOperator::kStrictEqual}, true,
        {{OperandRole::kLeft, value}, {OperandRole::kRight, value}});
    fixture.Add(1, 1, 61, ValueReturn{}, false,
        {{OperandRole::kReturnValue, *fixture.graph.ops[comparison.value].result}});
    fixture.Add(2, 0, 89, ValueReturn{}, false, {{OperandRole::kReturnValue, value}});
    fixture.graph.blocks = {{{0}, order, {0}, 2}, {{order}, 2, {2}, 0}, {{order + 2}, 1, {2}, 0}};
    fixture.graph.edges = {{{0}, {2}, FlowEdgeKind::kTaken, 0}, {{0}, {1}, FlowEdgeKind::kFallthrough, 1}};
    auto syntax = fixture.Syntax(); syntax.callable = CallableKind::kFunction;
    if (rest) {
      syntax.names.push_back(u"args");
      syntax.parameters.push_back({NameId{2}, NameEvidence::kDebugRange});
      syntax.parameters.back().rest = true;
    } else syntax.inferred_name = NameId{1};
    const auto control = std::get<ControlPlan>(BuildControlPlan(fixture.graph));
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(fixture.graph));
    auto built = BuildFunctionTree(fixture.graph, control, evaluation, syntax, {fixture.constants, static_cast<std::uint32_t>(syntax.names.size()), 0, {}});
    Check(std::holds_alternative<ConstructedFunction>(built));
    const auto& tree = std::get<ConstructedFunction>(built).tree;
    Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
    const auto native = rest ? tree.parameters[1].symbol : *tree.self;
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, rest ? 2 : 1, {}));
    Check(std::holds_alternative<ConstructedAbi>(BuildFunctionAbi(fixture.graph, tree, frame, 1)));
    const auto expression = std::get<ExprId>(tree.operations[0].node);
    Check(std::get<SymbolExpr>(tree.expressions[expression.value].expression).symbol == native);
    Check(tree.symbols[native.value].kind == (rest ? BindingKind::kParameter : BindingKind::kFunctionSelf));
  }
}
void RetainedReceiverBranches() {
  using namespace analysis;
  for (const bool initialized : {false, true}) for (const bool copy : {false, true}) {
    Fixture fixture;
    fixture.Add(0, 0, 8, initialized ? ValueOperation{ValueSuperCall{0, {}}}
        : ValueOperation{ValueFrameThis{}}, true, {}, initialized ? LanguageEffect::kUserCode : LanguageEffect::kNone);
    ValueId value{2}; auto order = 1u;
    if (copy) {
      const auto copied = fixture.Add(0, order++, 13, ValueCopy{}, true, {{OperandRole::kInput, value}});
      value = *fixture.graph.ops[copied.value].result;
    }
    const auto test = fixture.Add(0, order++, 29, ValueBinary{BinaryOperator::kStrictEqual}, true,
        {{OperandRole::kLeft, ValueId{0}}, {OperandRole::kRight, SpecialValue::kNull}});
    fixture.Add(0, order++, 37, ValueBranch{UnaryOperator::kTrueOrNonzeroNumber}, false,
        {{OperandRole::kCondition, *fixture.graph.ops[test.value].result}});
    const auto comparison = fixture.Add(1, 0, 54, ValueBinary{BinaryOperator::kStrictEqual}, true,
        {{OperandRole::kLeft, value}, {OperandRole::kRight, value}});
    fixture.Add(1, 1, 61, ValueReturn{}, false,
        {{OperandRole::kReturnValue, *fixture.graph.ops[comparison.value].result}});
    fixture.Add(2, 0, 89, ValueReturn{}, false, {{OperandRole::kReturnValue, value}});
    fixture.graph.blocks = {{{0}, order, {0}, 2}, {{order}, 2, {2}, 0}, {{order + 2}, 1, {2}, 0}};
    fixture.graph.edges = {{{0}, {2}, FlowEdgeKind::kTaken, 0}, {{0}, {1}, FlowEdgeKind::kFallthrough, 1}};
    auto syntax = fixture.Syntax();
    syntax.callable = initialized ? CallableKind::kDerivedConstructor : CallableKind::kFunction;
    const auto control = std::get<ControlPlan>(BuildControlPlan(fixture.graph));
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(fixture.graph));
    auto built = BuildFunctionTree(fixture.graph, control, evaluation, syntax, {fixture.constants, static_cast<std::uint32_t>(syntax.names.size()), 0, {}});
    Check(std::holds_alternative<ConstructedFunction>(built));
    const auto& tree = std::get<ConstructedFunction>(built).tree;
    Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
    Check(tree.symbols.size() == 1);
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {}));
    Check(std::holds_alternative<ConstructedAbi>(BuildFunctionAbi(fixture.graph, tree, frame, 1)));
    const auto comparison_expr = std::get<ExprId>(tree.operations[comparison.value].node);
    const auto right = std::get<BinaryExpr>(tree.expressions[comparison_expr.value].expression).right;
    Check(std::get<ThisExpr>(tree.expressions[right.value].expression).owner == tree.owner);
    auto wrong = tree;
    wrong.expressions[right.value].expression = SymbolExpr{tree.parameters[0].symbol};
    Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(wrong)));
  }
}
void FrameReceiverTree() {
  using namespace analysis;
  Fixture fixture;
  fixture.Add(0, 0, 0, ValueFrameThis{}, true, {});
  fixture.Add(0, 1, 1, ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{2}}});
  fixture.graph.blocks = {{{0}, 2, {0}, 0}};
  auto built = fixture.Build();
  auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
  const auto frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(0, 1, {false, false, false, false}));
  Check(!frame.this_value);
  const auto abi = std::get<ConstructedAbi>(BuildFunctionAbi(fixture.graph, tree, frame, 1));
  Check(abi.abi.receiver.operands == 0 && abi.abi.frame_receiver_reads == 1 &&
      abi.abi.receiver.references == 1);
  const auto expression = std::get<ExprId>(tree.operations[0].node);
  auto& receiver = std::get<ThisExpr>(tree.expressions[expression.value].expression);
  ++receiver.owner.value;
  Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(tree)));
  --receiver.owner.value;
  fixture.graph.values[2].domain = ValueKinds::Unknown();
  Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(tree)));
  fixture.graph.values[2].domain = ValueKinds::Language();
  for (const auto kind : {CallableKind::kArrow, CallableKind::kAsyncArrow, CallableKind::kDerivedConstructor}) {
    tree.callable = kind;
    Check(std::get<FunctionAbiError>(BuildFunctionAbi(fixture.graph, tree, frame, 1)) ==
        FunctionAbiError::kReceiver);
  }
}
void RetainedReceiverTrees() {
  using namespace analysis;
  for (const bool initialized : {false, true}) for (const bool split : {false, true})
      for (const bool copy : {false, true}) for (const bool getter : {false, true}) {
    Fixture fixture;
    fixture.Add(0, 0, 8, initialized ? ValueOperation{ValueSuperCall{0, {}}} :
        ValueOperation{ValueFrameThis{}}, true, {}, initialized ? LanguageEffect::kUserCode : LanguageEffect::kNone);
    ValueId receiver{2}; auto order = 1u;
    if (copy) {
      const auto copied = fixture.Add(0, order++, 13, ValueCopy{}, true, {{OperandRole::kInput, receiver}});
      receiver = *fixture.graph.ops[copied.value].result;
    }
    if (split) fixture.Add(0, order++, 21, ValueJump{}, false, {});
    const auto prefix = order; const auto block = split ? 1u : 0u;
    if (split) order = 0;
    if (getter) {
      const auto callee = fixture.Add(block, order++, 34, ValueNamedRead{{0}}, true,
          {{OperandRole::kBase, receiver}}, kRead);
      fixture.Add(block, order++, 41, ValueInvoke{InvokeReceiver::kExplicit, 0}, true,
          {{OperandRole::kCallee, *fixture.graph.ops[callee.value].result}, {OperandRole::kReceiver, receiver}},
          LanguageEffect::kUserCode);
    }
    const auto comparison = fixture.Add(block, order++, 49, ValueBinary{BinaryOperator::kStrictEqual}, true,
        {{OperandRole::kLeft, receiver}, {OperandRole::kRight, receiver}});
    fixture.Add(block, order++, 61, ValueReturn{}, false,
        {{OperandRole::kReturnValue, *fixture.graph.ops[comparison.value].result}});
    if (split) {
      fixture.graph.blocks = {{{0}, prefix, {0}, 1}, {{prefix}, order, {1}, 0}};
      fixture.graph.edges = {{{0}, {1}, FlowEdgeKind::kTaken, 0}};
    } else fixture.graph.blocks = {{{0}, order, {0}, 0}};
    auto syntax = fixture.Syntax();
    syntax.callable = initialized ? CallableKind::kDerivedConstructor : CallableKind::kFunction;
    const auto control = std::get<ControlPlan>(BuildControlPlan(fixture.graph));
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(fixture.graph));
    Check(evaluation.operations[0].temporary.has_value());
    const auto built = BuildFunctionTree(fixture.graph, control, evaluation, syntax, {fixture.constants, 2});
    Check(std::holds_alternative<ConstructedFunction>(built));
    const auto& tree = std::get<ConstructedFunction>(built).tree;
    Check(tree.symbols.size() == 1 && std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
    const auto binary = std::get<ExprId>(tree.operations[comparison.value].node);
    const auto right = std::get<BinaryExpr>(tree.expressions[binary.value].expression).right;
    Check(std::get<ThisExpr>(tree.expressions[right.value].expression).owner == tree.owner);
    auto wrong = tree; wrong.expressions[right.value].expression = SymbolExpr{tree.parameters[0].symbol};
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(wrong)));
    Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(wrong)));
    wrong = tree;
    const auto original = std::get<ExprId>(tree.operations[0].node);
    wrong.expressions[original.value].expression = ThisExpr{tree.owner};
    if (initialized) Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(wrong)));
    if (!initialized) {
      std::ostringstream text;
      Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, text)));
      Check(text.str().find("$value") == std::string::npos);
    }
  }
}
}  // namespace
int main(int argc,char** argv) {
  if(argc==2&&std::string_view(argv[1])=="--catch") {
    for(unsigned form=0;form<5;++form)for(bool split:{false,true})for(bool reallocated:{false,true})
      for(unsigned copies:{0U,1U})OriginalCatchValues(true,form,copies,split,reallocated);
    for(bool split:{false,true})for(bool reallocated:{false,true})
      for(unsigned copies:{200U,201U,400U,800U})OriginalCatchValues(true,0,copies,split,reallocated);
    return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--definitions") {
    for(bool action:{false,true}) {
      for(const unsigned copies:{0U,1U,200U,201U,400U,800U})
        for(const bool reverse:{false,true})for(const bool split:{false,true})for(const bool arena:{false,true})OriginalSuccessfulDomains(copies,reverse,split,false,true,arena,action);
      for(const bool reverse:{false,true})for(const bool arena:{false,true})OriginalSuccessfulDomains(1,reverse,false,true,true,arena,action);
    }
    return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--original") {
    for(const unsigned copies:{0U,1U,200U,201U,400U,800U})
      for(const bool reverse:{false,true})for(const bool split:{false,true})for(const bool numeric:{false,true})for(const bool arena:{false,true})
        OriginalNumericSnapshots(true,copies,reverse,split,numeric,arena);
    return 0;
  }
  RetainedReceiverTrees();
  RetainedReceiverBranches();
  ReceiverFields();
  SuperTree();
  ConstructorGuardAnchors();
  DerivedEntryValue();
  UnprovedTryRefusal();
  for (const analysis::ValueOperation operation : {
      analysis::ValueOperation{analysis::ValueGlobalObject{}},
      analysis::ValueOperation{analysis::ValueSymbolConstructor{}}}) {
    Fixture fixture;
    fixture.Add(0, 0, 0, operation, true, {});
    fixture.Add(0, 1, 1, analysis::ValueReturn{}, false,
        {{analysis::OperandRole::kReturnValue, analysis::ValueId{2}}});
    fixture.graph.blocks = {{{0}, 2, {0}, 0}};
    auto tree = std::get<ConstructedFunction>(fixture.Build()).tree;
    Check(std::holds_alternative<TreeSemantics>(fixture.Verify(tree)));
    const auto expression = std::get<ExprId>(tree.operations.front().node);
    const bool realm = std::holds_alternative<analysis::ValueGlobalObject>(operation);
    Check(realm ? std::holds_alternative<RealmObjectExpr>(tree.expressions[expression.value].expression) :
        std::holds_alternative<SymbolConstructorExpr>(tree.expressions[expression.value].expression));
    tree.expressions[expression.value].expression = realm ? TreeExpression{SymbolConstructorExpr{}} : TreeExpression{RealmObjectExpr{}};
    Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(tree)));
    tree.expressions[expression.value].expression = GlobalNameExpr{{1}};
    Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(tree)));
    tree.expressions[expression.value].expression = ThisExpr{tree.owner};
    Check(std::holds_alternative<TreeSemanticError>(fixture.Verify(tree)));
  }
  for(bool reverse:{false,true})for(bool split:{false,true})OriginalSuccessfulDomains(1,reverse,split,false,false,false,true);
  OriginalNumericSnapshots();
  for(const unsigned copies:{0U,1U,200U,201U})
    for(const bool reverse:{false,true})for(const bool split:{false,true})OriginalSuccessfulDomains(copies,reverse,split);
  for(const bool reverse:{false,true})OriginalSuccessfulDomains(1,reverse,false,true);
  CreationSelfTree();
  FrameFunctionTree();
  InvocationBindingTrees();
  InvocationBranchBindings();
  FrameReceiverTree();
  DebuggerTree();
  ThrowTree();
  ComputedStoreTree();
  EmptyTakenBranch();
  SharedConditionTree();
  BranchTree();
  SnapshotTree();
  NameAndConstantRefusals();
  PredicateSnapshot();
  SemanticMutations();
  GlobalTree();
  LoopTree();
  FreshAggregates();
  OwnDefinitionRefusal();
  for (const auto kind : {CallCase::kValue, CallCase::kMember, CallCase::kConstruct})
    for (const std::uint32_t count : {0U, 1U, 3U, 200U, 201U, 4096U})
      for (const bool computed : {false, true}) CallTree(kind, count, computed);
}
