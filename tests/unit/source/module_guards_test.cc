#include "../../../src/source/function_values.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
void Run(std::uint32_t count, bool external, bool wide) {
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
  RouteInputIdentity identity;
  identity.profile = abc::ProfileId::kDynamic1206;
  identity.abc_version = {12, 0, 6, 0};
  constexpr abc::EntityOffset kMethod{300}, kRecord{100};
  const auto opcode = external ? (wide ? abc::Opcode::kWIDE_LDEXTERNALMODULEVAR_PREF_IMM16 :
      abc::Opcode::kLDEXTERNALMODULEVAR_IMM8) : (wide ? abc::Opcode::kWIDE_LDLOCALMODULEVAR_PREF_IMM16 :
      abc::Opcode::kLDLOCALMODULEVAR_IMM8);
  constexpr auto kGuard = abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16;
  constexpr std::uint8_t kSlot = 7, kNameLow = 3, kNameHigh = 0;
  constexpr unsigned kByteBits = 8;
  std::vector<std::uint8_t> bytes;
  const auto prefixed = [&](abc::Opcode op) {
    bytes.push_back(static_cast<std::uint8_t>(op));
    bytes.push_back(static_cast<std::uint16_t>(op) >> kByteBits);
  };
  for (std::uint32_t i = 0; i < count; ++i) {
    if (wide) prefixed(opcode); else bytes.push_back(static_cast<std::uint8_t>(opcode));
    bytes.push_back(kSlot);
    if (wide) bytes.push_back(0);
    prefixed(kGuard); bytes.insert(bytes.end(), {kNameLow, kNameHigh});
  }
  bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED));
  const auto body = test::Body(bytes);
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, kMethod, kRecord,
      body, frame, InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& plan = values.modules;
  test::Check(plan.pairs.size() == count);
  const auto checked = VerifyModuleGuards(input, values.graph, plan);
  test::Check(std::holds_alternative<std::uint64_t>(checked));
  SemanticBudget budget;
  budget.work = std::get<std::uint64_t>(checked);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyModuleGuards(input, values.graph, plan, budget)));
  if (count == 0) return;
  --budget.work;
  test::Check(std::get<ModuleGuardError>(VerifyModuleGuards(input, values.graph, plan, budget)) == ModuleGuardError::kWorkLimit);
  auto changed_plan = plan;
  changed_plan.pairs.pop_back();
  test::Check(std::get<ModuleGuardError>(VerifyModuleGuards(input, values.graph, changed_plan)) == ModuleGuardError::kCoverage);
  changed_plan = plan; ++changed_plan.revision.value;
  test::Check(std::get<ModuleGuardError>(VerifyModuleGuards(input, values.graph, changed_plan)) == ModuleGuardError::kRevision);
  changed_plan = plan; changed_plan.pairs.front().guard.reset();
  test::Check(std::holds_alternative<ModuleGuardError>(VerifyModuleGuards(input, values.graph, changed_plan)));
  const auto pair = plan.pairs.front();
  const auto reject = [&](const ValueProgram& graph) {
    test::Check(std::holds_alternative<ModuleGuardError>(VerifyModuleGuards(input, graph, plan)));
  };
  auto graph = values.graph;
  graph.ops[pair.guard->value].life = ValueLife::kRemoved; reject(graph);
  graph = values.graph;
  ++std::get<ValueModuleRead>(graph.ops[pair.read.value].operation).slot.value; reject(graph);
  graph = values.graph;
  std::get<ValueGuard>(graph.ops[pair.guard->value].operation).name.reset(); reject(graph);
  graph = values.graph;
  graph.uses[graph.ops[pair.guard->value].first_use.value].operand = SpecialValue::kUndefined; reject(graph);
  graph = values.graph;
  graph.ops[pair.guard->value].before_throw = LanguageEffect::kNone; reject(graph);
  graph = values.graph;
  ++graph.ops[pair.guard->value].order; reject(graph);
  graph = values.graph;
  ++graph.origins[graph.ops[pair.read.value].origin.value].pc.value; reject(graph);
  graph = values.graph;
  graph.input.sites[0].reset(); reject(graph);
  if (count > 1) {
    changed_plan = plan; changed_plan.pairs[1] = changed_plan.pairs[0];
    test::Check(std::holds_alternative<ModuleGuardError>(VerifyModuleGuards(input, values.graph, changed_plan)));
    graph = values.graph;
    graph.uses[graph.ops[pair.guard->value].first_use.value].operand = *graph.ops[plan.pairs[1].read.value].result;
    reject(graph);
  }
  budget = {}; budget.bytes = 0;
  test::Check(std::get<ModuleGuardError>(BuildModuleGuards(input, values.graph, budget)) == ModuleGuardError::kResourceLimit);
  if (count == 1 && !wide) {
    const auto raw_body = test::Body({static_cast<std::uint8_t>(opcode), kSlot,
        static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED)});
    const auto raw_input = std::get<FunctionInput>(FunctionInput::Build(identity, kMethod, kRecord,
        raw_body, frame, InputCaptureMode::kClosed));
    const auto raw = std::get<FunctionValues>(BuildFunctionValues(raw_input, raw_body, frame));
    test::Check(raw.modules.pairs.empty());
    test::Check(std::holds_alternative<ValueModuleRead>(raw.graph.ops[0].operation) &&
        raw.graph.ops[0].life == ValueLife::kLive);
  }
}
}  // namespace
int main() {
  for (auto count : {0U, 1U, 2U, 200U, 201U, 4096U})
    for (bool external : {false, true}) for (bool wide : {false, true}) Run(count, external, wide);
}
