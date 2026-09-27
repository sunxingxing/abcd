#include "../../../src/source/function_values.h"
#include "../../../src/source/evaluation_plan.h"
#include "../../../src/source/module_evaluation.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
void Run(std::uint32_t count, bool external) {
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
        kSlot, kSlot, static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), kSaved,
        static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), kCallee,
        static_cast<std::uint8_t>(abc::Opcode::kCALLARG0_IMM8), kCache});
  }
  if (count) bytes.insert(bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), kSaved,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
  else bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED));
  const auto body = test::Body(bytes);
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body, frame,
      InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  std::vector<ModuleEvaluation> modules;
  for (const auto& pair : values.modules.pairs)
    modules.push_back({pair, std::get<ValueModuleRead>(graph.ops[pair.read.value].operation), {{7}, {0}}});
  test::Check(modules.size() == count);
  if (count) test::Check(std::get<EvaluationError>(BuildEvaluationPlan(graph)) == EvaluationError::kUnsupportedOperation);
  const auto plan = std::get<EvaluationPlan>(BuildEvaluationPlan(graph, {}, IncomingCells::kReject, {}, modules));
  const auto checked = VerifyEvaluationPlan(graph, plan);
  test::Check(std::holds_alternative<std::uint64_t>(checked));
  for (const auto& module : modules) {
    test::Check(plan.operations[module.pair.read.value].kind == EvaluationKind::kRoot);
    test::Check(plan.operations[module.pair.read.value].temporary.has_value() ==
        (module.pair.read == modules.back().pair.read));
    test::Check(plan.operations[module.pair.guard.value].kind == EvaluationKind::kModuleGuard);
    test::Check(!plan.operations[module.pair.guard.value].temporary);
  }
  SemanticBudget budget;
  budget.work = std::get<std::uint64_t>(checked);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph, plan, budget)));
  --budget.work;
  test::Check(std::get<EvaluationError>(VerifyEvaluationPlan(graph, plan, budget)) == EvaluationError::kWorkLimit);
  if (!count) return;
  auto changed = plan;
  changed.modules.clear();
  test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, changed)));
  changed = plan; changed.operations[modules[0].pair.guard.value].kind = EvaluationKind::kRemoved;
  test::Check(std::get<EvaluationError>(VerifyEvaluationPlan(graph, changed)) == EvaluationError::kOwnership);
  changed = plan; changed.modules[0].read.space = external ? ModuleSpace::kLocal : ModuleSpace::kExternal;
  test::Check(std::get<EvaluationError>(VerifyEvaluationPlan(graph, changed)) == EvaluationError::kOwnership);
  changed = plan; ++changed.modules[0].pair.guard.value;
  test::Check(std::get<EvaluationError>(VerifyEvaluationPlan(graph, changed)) == EvaluationError::kOwnership);
  auto broken = graph;
  broken.ops[modules[0].pair.guard.value].exceptional = ExceptionalCompletion::kImpossible;
  test::Check(std::get<EvaluationError>(VerifyEvaluationPlan(broken, plan)) == EvaluationError::kOrder);
  broken = graph;
  std::get<ValueGuard>(broken.ops[modules[0].pair.guard.value].operation).name.reset();
  test::Check(std::get<EvaluationError>(VerifyEvaluationPlan(broken, plan)) == EvaluationError::kInvalidGraph);
  budget = {}; budget.bytes = 0;
  test::Check(std::get<EvaluationError>(IndexModuleEvaluations(graph, modules, budget)) == EvaluationError::kResourceLimit);
  // Extra/repeated entries do not silently change source binding ownership.
  modules.push_back(modules.front());
  test::Check(std::get<EvaluationError>(IndexModuleEvaluations(graph, modules)) == EvaluationError::kOwnership);
}
}  // namespace
int main() {
  for (auto count : {0U, 1U, 2U, 200U, 201U, 4096U})
    for (bool external : {false, true}) Run(count, external);
}
