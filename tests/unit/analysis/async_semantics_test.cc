#include "abcd/analysis/exception_ssa.h"
#include "abcd/analysis/exception_value_flow.h"
#include "abcd/analysis/semantic_validation.h"
#include "abcd/analysis/await_catch.h"
#include "abcd/analysis/coroutine.h"
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
enum class HandlerRead { kNone, kClosedLoop, kResult, kMode, kRange, kUnknownEffects, kUnknownCoroutine, kBackEdge, kExceptionBackEdge };
void AwaitHandlerBranches(std::uint32_t branches, HandlerRead read, std::uint32_t awaits = 1,
                          bool handler_await = false) {
  const auto op = [](abc::Opcode value) { return static_cast<std::uint8_t>(value); };
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  constexpr unsigned kByteBits = 8;
  constexpr std::uint32_t kRegisters = 7;
  // Full private-state protocol; user catch preserves the caught accumulator
  // in v4. Only v1/v2 are the erased reject-resume definitions under test.
  std::vector<std::uint8_t> bytes{
      op(abc::Opcode::kASYNCFUNCTIONENTER), op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kASYNCFUNCTIONAWAITUNCAUGHT_V8), 0,
      op(abc::Opcode::kSUSPENDGENERATOR_V8), 0, op(abc::Opcode::kLDA_V8), 0,
      op(abc::Opcode::kRESUMEGENERATOR), op(abc::Opcode::kSTA_V8), 1,
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kGETRESUMEMODE), op(abc::Opcode::kSTA_V8), 2,
      op(abc::Opcode::kLDAI_IMM32), static_cast<std::uint8_t>(CoroutineResumeMode::kThrow), 0, 0, 0,
      op(abc::Opcode::kEQ_IMM8_V8), 0, 2, op(abc::Opcode::kJEQZ_IMM8), 6,
      op(abc::Opcode::kLDA_V8), 1,
      static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
      op(abc::Opcode::kLDA_V8), 1,
      op(abc::Opcode::kASYNCFUNCTIONRESOLVE_V8), 0, op(abc::Opcode::kRETURN)};
  const std::vector<std::uint8_t> protocol(bytes.begin() + 3, bytes.end() - 3);
  for (std::uint32_t i = 1; i < awaits; ++i)
    bytes.insert(bytes.end() - 3, protocol.begin(), protocol.end());
  const auto handler_pc = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 4});
  for (std::uint32_t i = 0; i < branches; ++i) {
    // Both successors are real CFG alternatives. A backwards jump out of the
    // suffix reaches the continuation's v1 read and must not be called dead.
    const auto delta = static_cast<std::uint8_t>(i == 0 && read == HandlerRead::kBackEdge ? -10 :
        read == HandlerRead::kClosedLoop ? -3 : 2);
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDTRUE), op(abc::Opcode::kJEQZ_IMM8), 4,
        op(abc::Opcode::kJMP_IMM8), delta});
  }
  if (handler_await) bytes.insert(bytes.end(), {
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kASYNCFUNCTIONAWAITUNCAUGHT_V8), 0,
      op(abc::Opcode::kSUSPENDGENERATOR_V8), 0, op(abc::Opcode::kLDA_V8), 0,
      op(abc::Opcode::kRESUMEGENERATOR), op(abc::Opcode::kSTA_V8), 5,
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kGETRESUMEMODE), op(abc::Opcode::kSTA_V8), 6,
      op(abc::Opcode::kLDAI_IMM32), static_cast<std::uint8_t>(CoroutineResumeMode::kThrow), 0, 0, 0,
      op(abc::Opcode::kEQ_IMM8_V8), 0, 6, op(abc::Opcode::kJEQZ_IMM8), 6,
      op(abc::Opcode::kLDA_V8), 5,
      static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
      op(abc::Opcode::kLDA_V8), 5});
  const std::uint8_t observed = read == HandlerRead::kResult ? 1 : read == HandlerRead::kMode ? 2 : 4;
  if (read == HandlerRead::kRange)
    bytes.insert(bytes.end(), {op(abc::Opcode::kCALLRANGE_IMM8_IMM8_V8), 0, 2, 1});
  else if (read == HandlerRead::kUnknownEffects)
    bytes.push_back(op(abc::Opcode::kGETUNMAPPEDARGS));
  else if (read == HandlerRead::kUnknownCoroutine)
    bytes.push_back(op(abc::Opcode::kRESUMEGENERATOR));
  else bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), observed});
  bytes.insert(bytes.end(), {op(abc::Opcode::kASYNCFUNCTIONRESOLVE_V8), 0, op(abc::Opcode::kRETURN)});
  const auto completion_pc = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 4,
      op(abc::Opcode::kASYNCFUNCTIONREJECT_V8), 0, op(abc::Opcode::kRETURN)});
  auto body = test::Body(bytes);
  abc::TryRegion user; user.offset = {3}; user.length = handler_pc - 6; user.handler_count = 1;
  abc::CatchHandler caught; caught.offset = {handler_pc}; caught.length = completion_pc - handler_pc;
  body.tries.push_back(user); body.handlers.push_back(caught);
  if (read == HandlerRead::kExceptionBackEdge) {
    abc::TryRegion inner; inner.offset = {handler_pc}; inner.length = completion_pc - handler_pc - 1;
    inner.first_handler = 1; inner.handler_count = 1;
    abc::CatchHandler back; back.offset = {33}; back.length = 2;
    body.tries.push_back(inner); body.handlers.push_back(back);
  }
  abc::TryRegion completion; completion.offset = {3}; completion.length = completion_pc - 3;
  completion.first_handler = static_cast<std::uint32_t>(body.handlers.size()); completion.handler_count = 1;
  abc::CatchHandler rejected; rejected.offset = {completion_pc}; rejected.length = 5;
  body.tries.push_back(completion); body.handlers.push_back(rejected);
  const auto found = FindAwaitRegions(body, kProfile, kRegisters, SemanticBudget{}.bytes);
  test::Check(std::holds_alternative<std::vector<AwaitRegion>>(found));
  const auto& regions = std::get<std::vector<AwaitRegion>>(found);
  test::Check(regions.size() == awaits + (handler_await ? 1U : 0U));
  const auto result = ProveAwaitCatchInputs(body, regions, kProfile, kRegisters, {completion_pc});
  if (read == HandlerRead::kNone || read == HandlerRead::kClosedLoop)
    test::Check(std::holds_alternative<AwaitCatchProof>(result));
  else {
    test::Check(std::holds_alternative<AwaitCatchFailure>(result));
    test::Check(std::get<AwaitCatchFailure>(result).kind == AwaitCatchError::kObservedRegister);
  }
}
}  // namespace

int main() {
  LiftLifecycle();
  SettlementAndFailures();
  ExceptionState(false);
  ExceptionState(true);
  RepeatedEntry();
  for (const auto branches : {1U, 200U, 201U, 402U}) AwaitHandlerBranches(branches, HandlerRead::kNone);
  for (const auto count : {200U, 201U, 402U}) AwaitHandlerBranches(count, HandlerRead::kNone, count);
  for (const auto count : {1U, 200U, 201U, 402U}) AwaitHandlerBranches(count, HandlerRead::kNone, count, true);
  AwaitHandlerBranches(1, HandlerRead::kClosedLoop);
  for (const auto read : {HandlerRead::kResult, HandlerRead::kMode, HandlerRead::kRange,
                          HandlerRead::kUnknownEffects, HandlerRead::kUnknownCoroutine, HandlerRead::kBackEdge,
                          HandlerRead::kExceptionBackEdge}) {
    AwaitHandlerBranches(1, read);
    AwaitHandlerBranches(1, read, 1, true);
  }
}
