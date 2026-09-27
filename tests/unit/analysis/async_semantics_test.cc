#include "abcd/analysis/exception_ssa.h"
#include "abcd/analysis/exception_value_flow.h"
#include "abcd/analysis/semantic_validation.h"
#include "../../../src/analysis/value_operands.h"

#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
constexpr auto kProfile = abc::ProfileId::kDynamic1301;
constexpr std::uint32_t kFrameSize = 2;

void LiftLifecycle() {
  // Pinned v7 encodings: enter; sta v0; ldundefined; resolve v0; return.
  const auto body = test::Body({0xae, 0x61, 0x00, 0x00, 0xcd, 0x00, 0x64});
  auto result = BuildSemanticMethod(body, kProfile, kFrameSize);
  test::Check(std::holds_alternative<SemanticMethod>(result));
  const auto& method = std::get<SemanticMethod>(result);
  test::Check(std::holds_alternative<AsyncEnter>(method.instructions.front().operation));
  const auto& settle = std::get<AsyncSettle>(method.instructions[3].operation);
  test::Check(settle.kind == AsyncSettlement::kResolve);
  test::Check(std::get<abc::RegisterId>(settle.state).value == 0);
  test::Check(std::holds_alternative<Accumulator>(settle.value));
  test::Check(method.instructions.size() == body.instructions.size());
  for (std::size_t i = 0; i < body.instructions.size(); ++i)
    test::Check(method.instructions[i].pc == body.instructions[i].offset);
  const auto& enter = method.instructions.front().effects;
  test::Check(enter.exceptional == ExceptionalCompletion::kMayThrow);
  test::Check(enter.normal == LanguageEffect::kAllocation);
  test::Check(enter.before_throw == enter.normal);
  test::Check(enter.normal_registers.read_count == 0);
  test::Check(enter.normal_registers.write &&
      std::holds_alternative<Accumulator>(*enter.normal_registers.write));
  // Raw protocol operations alone never grant source permission.
  const auto proof = VerifySemanticSource(body, method, kFrameSize, kFrameSize);
  test::Check(std::get<SemanticProofFailure>(proof).kind == SemanticProofError::kRegisterValue);
  test::Check(std::get<SemanticProofFailure>(proof).pc == abc::CodeOffset{0});
  for (const auto index : {0u, 3u})
    test::Check(std::get<OperandError>(DescribeValueOperands(
        method.instructions[index])) == OperandError::kUnsupportedOperation);
}

ExceptionNodeId Phase(const ExceptionFlow& flow, abc::CodeOffset pc,
                      ExceptionPhase phase) {
  for (std::size_t i = 0; i < flow.nodes.size(); ++i) {
    const auto& node = flow.nodes[i];
    if (node.phase == phase && flow.instructions[node.instruction.value].pc == pc)
      return {static_cast<std::uint32_t>(i)};
  }
  test::Check(false);
  return {};
}

void ExceptionState(bool normal_handler_entry) {
  // Try covers only resolve. Enter allocation failure must escape, not reject
  // using a state that was never created. A normal jump to handler code must
  // bypass the synthetic Catch definition and join with its own accumulator.
  auto body = normal_handler_entry
      ? test::Body({0xae, 0x61, 0x00, 0x00, 0xcd, 0x00, 0x4d, 0x02,
                    0xce, 0x00, 0x64})
      : test::Body({0xae, 0x61, 0x00, 0x00, 0xcd, 0x00, 0x64,
                    0xce, 0x00, 0x64});
  constexpr abc::CodeOffset kResolvePc{4};
  const abc::CodeOffset handler_pc{normal_handler_entry ? 8u : 7u};
  abc::TryRegion region;
  region.offset = kResolvePc;
  region.length = 2;  // op_v8 width, excluding the following return/jump.
  region.handler_count = 1;
  body.tries.push_back(region);
  abc::CatchHandler handler;
  handler.offset = handler_pc;
  handler.length = 3;  // reject v0; return.
  body.handlers.push_back(handler);
  auto built = BuildExceptionFlow(body, kProfile, kFrameSize);
  test::Check(std::holds_alternative<ExceptionFlow>(built));
  const auto& flow = std::get<ExceptionFlow>(built);
  const auto enter = Phase(flow, {0}, ExceptionPhase::kBefore);
  const auto state_store = Phase(flow, {1}, ExceptionPhase::kCommit);
  const auto before = Phase(flow, kResolvePc, ExceptionPhase::kBefore);
  const auto commit = Phase(flow, kResolvePc, ExceptionPhase::kCommit);
  const auto caught = Phase(flow, handler_pc, ExceptionPhase::kCatch);
  const auto rejected = Phase(flow, handler_pc, ExceptionPhase::kBefore);
  test::Check(flow.nodes[enter.value].exception.has_value());
  test::Check(!flow.nodes[enter.value].exception->target);
  test::Check(flow.nodes[before.value].successor_count == 2);
  bool saw_commit = false, saw_catch = false;
  for (const auto& edge : std::span(flow.nodes[before.value].successors).first(2)) {
    saw_commit |= edge.kind == ExceptionArcKind::kNormal && edge.target == commit;
    saw_catch |= edge.kind == ExceptionArcKind::kException && edge.target == caught;
  }
  test::Check(saw_commit && saw_catch);
  test::Check(flow.nodes[caught.value].successor_count == 1);
  test::Check(flow.nodes[caught.value].successors[0].target == rejected);
  IdfQuota quota{kDefaultSemanticWork};
  auto renamed = ExceptionSsa::Build(flow, kFrameSize, quota);
  test::Check(std::holds_alternative<ExceptionSsa>(renamed));
  const auto& ssa = std::get<ExceptionSsa>(renamed);
  test::Check(!ssa.Write(before));
  test::Check(ssa.Reads(before).size() == 2);
  test::Check(ssa.Reads(commit).empty());
  test::Check(ssa.Write(commit) && ssa.Write(caught));
  test::Check(ssa.Write(commit) != ssa.Write(caught));
  const auto reads = ssa.Reads(rejected);
  test::Check(reads.size() == 2);
  test::Check(reads[0].value == *ssa.Write(state_store));
  if (!normal_handler_entry) {
    test::Check(reads[1].value == *ssa.Write(caught));
  } else {
    const auto phi = reads[1].value;
    test::Check(std::holds_alternative<PhiValue>(ssa.Values()[phi.value].origin));
    const auto inputs = ssa.Inputs(phi);
    test::Check(inputs.size() == 2);
    bool saw_normal_value = false, saw_exception_value = false;
    for (const auto& input : inputs) {
      saw_normal_value |= input.value == *ssa.Write(commit);
      saw_exception_value |= input.value == *ssa.Write(caught);
    }
    test::Check(saw_normal_value && saw_exception_value);
  }
  // The phase topology can be analyzed, but a language-domain proof cannot
  // bless the raw async state under even the broadest legal-host premises.
  IdfQuota value_quota{kDefaultSemanticWork};
  test::Check(std::holds_alternative<ValueFlowFailure>(ExceptionValueFlow::Build(
      flow, ssa, {}, value_quota, {}, PendingExceptionValues::kLanguage, {}, {},
      CallResultValues::kLanguage)));
  test::Check(std::get<FlowFailure>(BuildExceptionFlow(
      body, kProfile, kFrameSize, {0})).kind == FlowError::kResourceLimit);
  test::Check(std::holds_alternative<SemanticFailure>(
      BuildExceptionFlow(body, kProfile, kFrameSize,
          {64 * 1024 * 1024, flow.work_units - 1})));
  test::Check(std::holds_alternative<ExceptionFlow>(
      BuildExceptionFlow(body, kProfile, kFrameSize,
          {64 * 1024 * 1024, flow.work_units})));
}

void SettlementAndFailures() {
  constexpr auto kEffects = LanguageEffect::kAllocation | LanguageEffect::kHeapRead |
      LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
  for (const auto bytes : {std::array<std::uint8_t, 2>{0xcd, 0x01},
                           std::array<std::uint8_t, 2>{0xce, 0x01}}) {
    const auto site = test::Body(bytes).instructions.front();
    for (const auto profile : {abc::ProfileId::kDynamic1202,
                               abc::ProfileId::kDynamic1206, kProfile}) {
      const auto result = LiftSemanticInstruction(site, profile, kFrameSize);
      test::Check(std::holds_alternative<SemanticInstruction>(result));
      const auto& instruction = std::get<SemanticInstruction>(result);
      const auto& settle = std::get<AsyncSettle>(instruction.operation);
      test::Check(settle.kind == (site.decoded.opcode == abc::Opcode::kASYNCFUNCTIONRESOLVE_V8
          ? AsyncSettlement::kResolve : AsyncSettlement::kReject));
      test::Check(std::get<abc::RegisterId>(settle.state).value == 1);
      const auto& effects = instruction.effects;
      test::Check(effects.normal == kEffects && effects.before_throw == kEffects);
      test::Check(effects.exceptional == ExceptionalCompletion::kMayThrow);
      test::Check(effects.normal_registers.read_count == 2);
      test::Check(std::get<abc::RegisterId>(effects.normal_registers.reads[0]).value == 1);
      test::Check(std::holds_alternative<Accumulator>(effects.normal_registers.reads[1]));
      test::Check(effects.normal_registers.write &&
          std::holds_alternative<Accumulator>(*effects.normal_registers.write));
    }
    auto malformed = site;
    malformed.decoded.operand_count = 0;
    test::Check(std::get<SemanticFailure>(LiftSemanticInstruction(
        malformed, kProfile, kFrameSize)).kind == SemanticError::kMalformedInstruction);
    malformed = site;
    malformed.decoded.operands[0].raw_bits = UINT8_MAX + 1u;
    test::Check(std::get<SemanticFailure>(LiftSemanticInstruction(
        malformed, kProfile, UINT8_MAX + 2u)).kind == SemanticError::kMalformedInstruction);
    malformed = site;
    malformed.decoded.operands[0].kind = abc::OperandKind::kImmediate;
    test::Check(std::get<SemanticFailure>(LiftSemanticInstruction(
        malformed, kProfile, kFrameSize)).kind == SemanticError::kMalformedInstruction);
    test::Check(std::get<SemanticFailure>(LiftSemanticInstruction(
        site, kProfile, 1)).kind == SemanticError::kMalformedInstruction);
    test::Check(std::get<SemanticFailure>(LiftSemanticInstruction(
        site, abc::ProfileId::kDynamic2400, kFrameSize)).kind == SemanticError::kUnsupportedProfile);
  }
  auto enter = test::Body({0xae}).instructions.front();
  enter.decoded.operand_count = 1;
  test::Check(std::get<SemanticFailure>(LiftSemanticInstruction(
      enter, kProfile, kFrameSize)).kind == SemanticError::kMalformedInstruction);
  // Historical settlement has a different operand contract. Do not admit it
  // by ignoring its extra operands or guessing a modern owner protocol.
  for (const auto opcode : {0x1eu, 0x1fu}) {
    const auto old = test::Body({0xfc, static_cast<std::uint8_t>(opcode), 0, 0, 0});
    test::Check(std::get<SemanticFailure>(LiftSemanticInstruction(
        old.instructions.front(), kProfile, kFrameSize)).kind ==
        SemanticError::kUnsupportedInstruction);
  }
}

void RepeatedEntry() {
  // Cardinality regression, not a benchmark. Each dynamic enter remains a
  // separate allocation/commit even without a consumer or an owner proof.
  for (const std::size_t count : {200u, 201u, 402u}) {
    std::vector<std::uint8_t> bytes(count, 0xae);
    bytes.push_back(0x64);  // return.
    const auto body = test::Body(bytes);
    auto built = BuildExceptionFlow(body, kProfile, kFrameSize);
    test::Check(std::holds_alternative<ExceptionFlow>(built));
    const auto& flow = std::get<ExceptionFlow>(built);
    test::Check(flow.instructions.size() == count + 1);
    std::size_t before = 0, commits = 0;
    for (const auto& node : flow.nodes) {
      if (!std::holds_alternative<AsyncEnter>(flow.instructions[node.instruction.value].operation))
        continue;
      if (node.phase == ExceptionPhase::kBefore) {
        ++before;
        test::Check(node.exception && !node.exception->target);
      } else {
        test::Check(node.phase == ExceptionPhase::kCommit);
        ++commits;
      }
    }
    test::Check(before == count && commits == count);
    IdfQuota quota{kDefaultSemanticWork};
    auto renamed = ExceptionSsa::Build(flow, kFrameSize, quota);
    test::Check(std::holds_alternative<ExceptionSsa>(renamed));
  }
}
}  // namespace

int main() {
  LiftLifecycle();
  SettlementAndFailures();
  ExceptionState(false);
  ExceptionState(true);
  RepeatedEntry();
}
