#include "../../../src/source/function_values.h"
#include "../../../src/source/evaluation_plan.h"
#include "../../../src/source/module_evaluation.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "analysis/ssa_fixture.h"
#include "source/action_regions.h"

#include <algorithm>

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
test::ActionRegionCost Run(std::uint32_t count, bool external, bool barrier = true) {
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(1, 1, {false, false, false, false}));
  RouteInputIdentity identity;
  identity.profile = abc::ProfileId::kDynamic1206;
  identity.abc_version = {12, 0, 6, 0};
  constexpr std::uint8_t kSlot = 0, kSaved = 0, kCallee = 1, kCache = 0;
  constexpr unsigned kByteBits = 8;
  constexpr auto kGuard = abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16;
  const auto load = external ? abc::Opcode::kLDEXTERNALMODULEVAR_IMM8 : abc::Opcode::kLDLOCALMODULEVAR_IMM8;
  std::vector<std::uint8_t> bytes;
  for (std::uint32_t i = 0; i < count; ++i) {
    // Save the checked read, then call a function that may change the module
    // binding. The return must retain the earlier snapshot, not reread it.
    bytes.insert(bytes.end(), {static_cast<std::uint8_t>(load), kSlot,
        static_cast<std::uint8_t>(kGuard), static_cast<std::uint8_t>(static_cast<std::uint16_t>(kGuard) >> kByteBits),
        kSlot, kSlot, static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), kSaved});
    if (barrier) bytes.insert(bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), kCallee,
        static_cast<std::uint8_t>(abc::Opcode::kCALLARG0_IMM8), kCache});
  }
  if (count) bytes.insert(bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), kSaved,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
  else bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED));
  const auto body = test::Body(bytes);
  // The 4096-pair fixture exhausts the default SSA work allowance before it
  // reaches evaluation. Fund this scaling fixture explicitly, not production.
  constexpr std::uint64_t kFixtureWork = kDefaultSemanticWork;
  const SemanticBudget fixture_budget{SemanticBudget{}.bytes, kFixtureWork};
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body, frame,
      InputCaptureMode::kClosed, fixture_budget));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame, fixture_budget));
  const auto& graph = values.graph;
  std::vector<ModuleEvaluation> modules;
  for (const auto& pair : values.modules.pairs)
    modules.push_back({pair, std::get<ValueModuleRead>(graph.ops[pair.read.value].operation), {{7}, {0}}});
  test::Check(modules.size() == count);
  if (count) test::Check(std::get<EvaluationFailure>(BuildEvaluationPlan(graph, fixture_budget)) == EvaluationError::kUnsupportedOperation);
  const auto plan = std::get<EvaluationPlan>(BuildEvaluationPlan(graph, fixture_budget, IncomingCells::kReject, {}, modules));
  const auto checked = VerifyEvaluationPlan(graph, plan, fixture_budget);
  test::Check(std::holds_alternative<std::uint64_t>(checked));
  for (const auto& module : modules) {
    test::Check(plan.operations[module.pair.read.value].kind ==
        (!barrier && module.pair.read == modules.back().pair.read ? EvaluationKind::kInline : EvaluationKind::kRoot));
    test::Check(plan.operations[module.pair.read.value].temporary.has_value() ==
        (barrier && module.pair.read == modules.back().pair.read));
    test::Check(plan.operations[module.pair.guard->value].kind == EvaluationKind::kModuleGuard);
    test::Check(!plan.operations[module.pair.guard->value].temporary);
  }
  FunctionSyntax syntax{};
  syntax.identity = identity; syntax.method = {300}; syntax.owner = {1};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.parameters.resize(frame.parameters);
  if (count) {
    syntax.names.push_back(u"moduleValue");
    syntax.modules.push_back({{{7}, {0}}, {0}, ModuleAccess::kReadOnly, NameEvidence::kSynthesized});
  }
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph, fixture_budget));
  std::vector<ExpressionConstant> operations(graph.ops.size());
  const ExpressionConstants constants{operations, syntax.names.size(), 0};
  const auto tree = std::get<ConstructedFunction>(BuildFunctionTree(graph, control, plan, syntax, constants, fixture_budget)).tree;
  const auto semantics=VerifyTreeSemantics(graph, control, plan, tree, constants, fixture_budget);
  test::Check(std::holds_alternative<TreeSemantics>(semantics));
  const auto cost=count<=800?test::CheckActionRegions(tree,*std::get<TreeSemantics>(semantics).flow.original):test::ActionRegionCost{};
  const auto flow = std::get<TreeFlow>(VerifyTreeFlow(graph, control, plan, tree, fixture_budget));
  for (const auto& module : modules) {
    const auto read = module.pair.read.value, guard = module.pair.guard->value;
    test::Check(flow.expressions[read] && flow.expressions[read] == flow.expressions[guard] && !flow.statements[guard]);
    const auto expression = *flow.expressions[read];
    test::Check(std::get<SymbolExpr>(tree.expressions[expression.value].expression).symbol == tree.modules[0].symbol);
    const auto combined = std::get<CombinedOrigin>(tree.origins[tree.expressions[expression.value].origin.value]);
    test::Check(combined.part_count == 2 && tree.origin_parts[combined.first_part].value == graph.ops[read].origin.value &&
        tree.origin_parts[combined.first_part + 1].value == graph.ops[guard].origin.value);
    test::Check(flow.snapshots[read].has_value() == (barrier && read == modules.back().pair.read.value));
  }
  SemanticBudget budget = fixture_budget;
  budget.work = std::get<std::uint64_t>(checked);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph, plan, budget)));
  --budget.work;
  test::Check(std::get<EvaluationFailure>(VerifyEvaluationPlan(graph, plan, budget)) == EvaluationError::kWorkLimit);
  if (!count) return cost;
  const auto reject_tree = [&](const FunctionTree& candidate) {
    test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, plan, candidate, constants, fixture_budget)));
  };
  auto changed_tree = tree;
  ++changed_tree.modules[0].target.symbol.value; reject_tree(changed_tree);
  changed_tree = tree;
  const auto expression = *flow.expressions[modules[0].pair.read.value];
  changed_tree.expressions[expression.value].expression = GlobalNameExpr{{0}}; reject_tree(changed_tree);
  changed_tree = tree;
  const auto combined = std::get<CombinedOrigin>(tree.origins[tree.expressions[expression.value].origin.value]);
  changed_tree.origin_parts[combined.first_part + 1] = changed_tree.origin_parts[combined.first_part];
  reject_tree(changed_tree);
  changed_tree = tree;
  // Dropping the guard's placement loses its PC even if the read stays valid.
  changed_tree.operations.erase(std::remove_if(changed_tree.operations.begin(), changed_tree.operations.end(),
      [&](const auto& placement) { return placement.operation == modules[0].pair.guard; }), changed_tree.operations.end());
  reject_tree(changed_tree);
  auto changed = plan;
  changed.modules.clear();
  test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, changed, fixture_budget)));
  changed = plan; changed.operations[modules[0].pair.guard->value].kind = EvaluationKind::kRemoved;
  test::Check(std::get<EvaluationFailure>(VerifyEvaluationPlan(graph, changed, fixture_budget)) == EvaluationError::kOwnership);
  changed = plan; changed.modules[0].read.space = external ? ModuleSpace::kLocal : ModuleSpace::kExternal;
  test::Check(std::get<EvaluationFailure>(VerifyEvaluationPlan(graph, changed, fixture_budget)) == EvaluationError::kOwnership);
  changed = plan; ++changed.modules[0].pair.guard->value;
  test::Check(std::get<EvaluationFailure>(VerifyEvaluationPlan(graph, changed, fixture_budget)) == EvaluationError::kOwnership);
  auto broken = graph;
  broken.ops[modules[0].pair.guard->value].exceptional = ExceptionalCompletion::kImpossible;
  test::Check(std::get<EvaluationFailure>(VerifyEvaluationPlan(broken, plan, fixture_budget)) == EvaluationError::kOrder);
  broken = graph;
  std::get<ValueGuard>(broken.ops[modules[0].pair.guard->value].operation).name.reset();
  test::Check(std::get<EvaluationFailure>(VerifyEvaluationPlan(broken, plan, fixture_budget)) == EvaluationError::kInvalidGraph);
  budget = {}; budget.bytes = 0;
  test::Check(std::get<EvaluationFailure>(IndexModuleEvaluations(graph, modules, budget)) == EvaluationError::kResourceLimit);
  // Extra/repeated entries do not silently change source binding ownership.
  modules.push_back(modules.front());
  test::Check(std::get<EvaluationFailure>(IndexModuleEvaluations(graph, modules, fixture_budget)) == EvaluationError::kOwnership);
  return cost;
}
void Initialized() {
  const auto wire=[](abc::Opcode value){return static_cast<std::uint8_t>(value);};
  const auto body=test::Body({wire(abc::Opcode::kLDAI_IMM32),1,0,0,0,
      wire(abc::Opcode::kSTMODULEVAR_IMM8),0,wire(abc::Opcode::kLDLOCALMODULEVAR_IMM8),0,wire(abc::Opcode::kRETURN)});
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,0,{}));
  RouteInputIdentity identity;identity.profile=abc::ProfileId::kDynamic1206;
  const auto input=std::get<FunctionInput>(FunctionInput::Build(identity,{300},{100},body,frame,InputCaptureMode::kClosed));
  const auto values=std::get<FunctionValues>(BuildFunctionValues(input,body,frame));const auto& graph=values.graph;
  test::Check(values.modules.pairs.size()==1&&!values.modules.pairs.front().guard);
  const auto pair=values.modules.pairs.front();
  std::vector<ModuleEvaluation> modules{{pair,std::get<ValueModuleRead>(graph.ops[pair.read.value].operation),{{7},{0}}}};
  const auto evaluation=std::get<EvaluationPlan>(BuildEvaluationPlan(graph,{},IncomingCells::kReject,{},modules));
  const auto control=std::get<ControlPlan>(BuildControlPlan(graph));
  FunctionSyntax syntax{};syntax.identity=identity;syntax.method={300};syntax.owner={1};
  syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;syntax.names={u"initialized"};
  syntax.modules={ModuleSyntax{{{7},{0}},{0},ModuleAccess::kMutable,NameEvidence::kMetadata,true}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for(unsigned i=0;i<graph.ops.size();++i)if(const auto* store=std::get_if<ValueModuleWrite>(&graph.ops[i].operation))
    constants[i]=ModuleStoreConstant{store->slot,{{7},{0}},true};
  const auto constructed=std::get<ConstructedFunction>(BuildFunctionTree(graph,control,evaluation,syntax,{constants,1}));
  const auto& original=*constructed.flow->original;
  test::Check(original.action_regions.size()==1&&original.action_regions.front().entry==original.action_regions.front().exit);
  test::CheckActionRegions(constructed.tree,original);
}
}  // namespace
int main() {
  Initialized();
  for (auto count : {0U, 1U, 2U, 4096U})
    for (bool external : {false, true}) { Run(count, external); Run(count, external, false); }
  for(bool external:{false,true})for(bool barrier:{false,true}) {
    test::ActionRegionCost previous;
    for(auto count:{200U,201U,400U,800U}) {
      const auto cost=Run(count,external,barrier);
      if(previous.work) {
        const auto growth=count==201?1.1:3.0;
        test::Check(cost.work<=growth*previous.work&&cost.bytes<=growth*previous.bytes);
      }
      std::cout<<"action-regions "<<external<<' '<<barrier<<' '<<count<<' '<<cost.work<<' '<<cost.bytes<<'\n';
      previous=cost;
    }
  }
}
