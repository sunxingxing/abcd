#include "../../../src/source/function_values.h"
#include "analysis/ssa_fixture.h"

#include <algorithm>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
void StoredRead(std::uint32_t count, abc::ProfileId profile, bool wide, bool guarded) {
  RouteInputIdentity identity; identity.profile = profile;
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 0, {false, false, false, false}));
  const auto wire = [](abc::Opcode op) { return static_cast<std::uint8_t>(op); };
  std::vector<std::uint8_t> bytes{wire(abc::Opcode::kLDAI_IMM32), 1, 0, 0, 0};
  const auto slot_count = wide ? count : std::min(count, std::uint32_t{UINT8_MAX} + 1);
  const auto slot_op = [&](abc::Opcode op, std::uint16_t slot) {
    bytes.push_back(wire(op));
    if (wide) bytes.push_back(static_cast<std::uint16_t>(op) >> 8);
    bytes.push_back(static_cast<std::uint8_t>(slot));
    if (wide) bytes.push_back(static_cast<std::uint8_t>(slot >> 8));
  };
  for (std::uint32_t i = 0; i < count; ++i)
    slot_op(wide ? abc::Opcode::kWIDE_STMODULEVAR_PREF_IMM16 : abc::Opcode::kSTMODULEVAR_IMM8, i % slot_count);
  for (std::uint32_t i = 0; i < count; ++i) {
    slot_op(wide ? abc::Opcode::kWIDE_LDLOCALMODULEVAR_PREF_IMM16 : abc::Opcode::kLDLOCALMODULEVAR_IMM8, i % slot_count);
    if (guarded) {
      const auto op = abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16;
      bytes.insert(bytes.end(), {wire(op), static_cast<std::uint8_t>(static_cast<std::uint16_t>(op) >> 8), 3, 0});
    }
  }
  bytes.push_back(wire(abc::Opcode::kRETURN));
  const auto body = test::Body(bytes);
  const auto result = FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed);
  test::Check(std::holds_alternative<FunctionInput>(result));
  const auto& input = std::get<FunctionInput>(result);
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  test::Check(values.modules.pairs.size() == count && input.Proof().module_reads.size() == count);
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto& fact = input.Proof().module_reads[i];
    const auto& pair = values.modules.pairs[i];
    test::Check(pair.guard.has_value() == guarded);
    test::Check(fact.proof == (guarded ? ModuleReadProof::kGuarded : ModuleReadProof::kStored));
    test::Check(guarded ? !fact.store : fact.store == InstructionId{1 + i % slot_count});
    test::Check(values.graph.values[values.graph.ops[pair.read.value].result->value].domain == ValueKinds::Language());
  }
  test::Check(input.InitializedModuleReads().empty() && !input.HasOwner());
  const auto checked = VerifyModuleGuards(input, values.graph, values.modules);
  test::Check(std::holds_alternative<std::uint64_t>(checked));
  SemanticBudget exact; exact.work = std::get<std::uint64_t>(checked);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyModuleGuards(input, values.graph, values.modules, exact)));
  --exact.work;
  test::Check(std::get<ModuleGuardError>(VerifyModuleGuards(input, values.graph, values.modules, exact)) == ModuleGuardError::kWorkLimit);
  if (!guarded) {
    const auto store = *values.graph.input.sites[input.Proof().module_reads.front().store->value];
    auto wrong = values.graph; ++std::get<ValueModuleWrite>(wrong.ops[store.value].operation).slot.value;
    test::Check(std::holds_alternative<ModuleGuardError>(VerifyModuleGuards(input, wrong, values.modules)));
    wrong = values.graph; wrong.ops[store.value].life = ValueLife::kRemoved;
    test::Check(std::holds_alternative<ModuleGuardError>(VerifyModuleGuards(input, wrong, values.modules)));
    wrong = values.graph; wrong.ops[store.value].order = wrong.ops[values.modules.pairs.front().read.value].order;
    test::Check(std::holds_alternative<ModuleGuardError>(VerifyModuleGuards(input, wrong, values.modules)));
    wrong = values.graph; wrong.ops[store.value].block.value = 1;
    test::Check(std::holds_alternative<ModuleGuardError>(VerifyModuleGuards(input, wrong, values.modules)));
    wrong = values.graph; ++wrong.origins[wrong.ops[store.value].origin.value].pc.value;
    test::Check(std::holds_alternative<ModuleGuardError>(VerifyModuleGuards(input, wrong, values.modules)));
  }
}
void StoredReadFailures() {
  RouteInputIdentity identity; identity.profile = abc::ProfileId::kDynamic1206;
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 0, {false, false, false, false}));
  // WindowUtil's enum protocol starts with real undefined, not a VM Hole.
  const auto initialized = test::Body({0x00, 0x7c, 0, 0x7d, 0, 0x64});
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100},
      initialized, frame, InputCaptureMode::kClosed));
  test::Check(input.Proof().module_reads.size() == 1 &&
      input.Proof().module_reads.front().proof == ModuleReadProof::kStored &&
      input.Proof().module_reads.front().store == InstructionId{1});
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, initialized, frame));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyModuleGuards(input, values.graph, values.modules)));
  const auto read = values.modules.pairs.front().read;
  test::Check(values.graph.values[values.graph.ops[read.value].result->value].domain == ValueKinds::Language());
  auto wrong = values.graph;
  wrong.values[wrong.ops[read.value].result->value].domain = ValueKinds::Hole();
  test::Check(std::holds_alternative<ModuleGuardError>(VerifyModuleGuards(input, wrong, values.modules)));
  const auto reject = [&](std::initializer_list<std::uint8_t> bytes) {
    const auto body = test::Body(bytes);
    const auto result = FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed);
    const auto* failure = std::get_if<FunctionInputFailure>(&result);
    test::Check(failure && std::holds_alternative<SemanticProofFailure>(failure->cause));
    test::Check(std::get<SemanticProofFailure>(failure->cause).kind == SemanticProofError::kInternalValue);
  };
  reject({0x7d, 0, 0x7c, 0, 0x7d, 0, 0x64}); // Circular first initialization.
  reject({0x70, 0x7c, 0, 0x7d, 0, 0x64}); // Hole stored, not undefined.
  reject({0x00, 0x7c, 0, 0x70, 0x7c, 0, 0x7d, 0, 0x64}); // Later non-Language store.
  reject({0x00, 0x7c, 0, 0x7d, 1, 0x64}); // Different local slot.
  reject({0x00, 0x7c, 0, 0x7e, 0, 0x64}); // External slot is a different namespace.
  reject({0x7d, 0, 0x64, 0x00, 0x7c, 0, 0x65}); // Future unreachable store.
  reject({0x62, 1, 0, 0, 0, 0x4f, 5, 0x00, 0x7c, 0, 0x7d, 0, 0x64}); // Branch-local store.
}
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
  StoredReadFailures();
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto count : {1U, 200U, 201U, 400U})
      for (const bool wide : {false, true}) for (const bool guarded : {false, true}) StoredRead(count, profile, wide, guarded);
  for (auto count : {0U, 1U, 2U, 200U, 201U, 4096U})
    for (bool external : {false, true}) for (bool wide : {false, true}) Run(count, external, wide);
}
