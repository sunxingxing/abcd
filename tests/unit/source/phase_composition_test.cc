#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/phase_evaluation.h"
#include "../../../src/source/call_plan.h"
#include "analysis/ssa_fixture.h"
#include "abcd/analysis/arithmetic_semantics.h"

// Phased composition across a committed selection (call-references.md §5.4).
// Base: try { r = obj.f(1 + (c ? x : y)); } catch { return; } return r;
// Each variant breaks exactly one obligation of the shared order proof.
namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class Variant {
  kBase, kForeignBefore, kForeignAfterJoin, kCallOutsideTry, kCatchCopies,
  kExtraUse,  // The callee value is observed again after the call.
  kChain,     // A chain of Binaries over the selection, each split by a foreign getter.
  kPureChain  // A run of pure (non-event) typeof wrappers over the selection's Binary.
};
// Returns the planner's work, for linear-scaling checks.
std::uint64_t Run(abc::ProfileId profile, Variant variant, std::uint32_t chain = 0) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr std::uint8_t kCallee = 0, kLeft = 1, kArgument = 2, kResult = 3;
  constexpr std::uint8_t kObject = 7, kCondition = 8, kX = 9, kY = 10;
  // Prelude outside the try: the handler may read kLeft on every path.
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kLDAI_IMM32), 0, 0, 0, 0, op(abc::Opcode::kSTA_V8), kLeft};
  const auto try_begin = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kObject,
      op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0, op(abc::Opcode::kSTA_V8), kCallee,
      op(abc::Opcode::kLDAI_IMM32), 1, 0, 0, 0, op(abc::Opcode::kSTA_V8), kLeft});
  // A getter whose result is discarded: an ordered event outside the call tree.
  const auto foreign = [&] {
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kObject, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0});
  };
  if (variant == Variant::kForeignBefore) foreign();
  constexpr auto kIsFalse = static_cast<std::uint16_t>(abc::Opcode::kCALLRUNTIME_ISFALSE_PREF_IMM8);
  constexpr unsigned kByteBits = 8;
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kCondition, static_cast<std::uint8_t>(kIsFalse),
      static_cast<std::uint8_t>(kIsFalse >> kByteBits), 0, op(abc::Opcode::kJNEZ_IMM8), 6,
      op(abc::Opcode::kLDA_V8), kX, op(abc::Opcode::kJMP_IMM8), 4,
      op(abc::Opcode::kLDA_V8), kY});
  if (variant == Variant::kForeignAfterJoin) {
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), kArgument});
    foreign();
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kArgument});
  }
  bytes.insert(bytes.end(), {op(abc::Opcode::kADD2_IMM8_V8), 0, kLeft});
  for (std::uint32_t i = 0; variant == Variant::kChain && i < chain; ++i) {
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), kArgument});
    foreign();
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kArgument, op(abc::Opcode::kADD2_IMM8_V8), 0, kLeft});
  }
  if (variant == Variant::kPureChain)
    for (std::uint32_t i = 0; i < chain; ++i) bytes.insert(bytes.end(), {op(abc::Opcode::kTYPEOF_IMM8), 0});
  bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), kArgument});
  const auto before_call = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kCallee,
      op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, kObject, kArgument, op(abc::Opcode::kSTA_V8), kResult});
  if (variant == Variant::kExtraUse)
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kCallee, op(abc::Opcode::kSTA_V8), kResult});
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kResult, op(abc::Opcode::kRETURN)});
  const auto handler_pc = static_cast<std::uint32_t>(bytes.size());
  // Reading a register assigned inside the span gives its throw points catch copies.
  if (variant == Variant::kCatchCopies) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kLeft});
  bytes.push_back(op(abc::Opcode::kRETURN));
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {try_begin}; region.length = handler_pc - try_begin; region.handler_count = 1;
  if (variant == Variant::kCallOutsideTry) region.length = before_call - try_begin;
  abc::CatchHandler handler; handler.offset = {handler_pc};
  handler.length = variant == Variant::kCatchCopies ? 3 : 1;
  body.tries.push_back(region); body.handlers.push_back(handler);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(4, 7, {true, true, false, true}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers));
  IdfQuota ssa_work{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, ssa_work));
  std::vector<EntryValueFact> entries;
  for (std::size_t i = 0; i < ssa.Values().size(); ++i) {
    const auto& value = ssa.Values()[i];
    const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
    if (reg && reg->value >= frame.locals && std::holds_alternative<MethodEntry>(value.origin))
      entries.push_back({{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
  }
  const auto env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
      value_work, {}, PendingExceptionValues::kLanguage, {}, {}, CallResultValues::kLanguage,
      FrameReceiver::kUnknown, ArithmeticSemantics::kSourceNumeric));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, env, values));
  const PhasedValueInput input{flow, ssa, env, values, proof, frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  std::optional<OpId> callee, call;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (!callee && std::holds_alternative<ValueNamedRead>(graph.ops[i].operation)) callee = OpId{static_cast<std::uint32_t>(i)};
    if (std::holds_alternative<ValueInvoke>(graph.ops[i].operation)) call = OpId{static_cast<std::uint32_t>(i)};
  }
  test::Check(callee && call);
  const auto evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph, {}, IncomingCells::kReject));
  const bool composed = variant == Variant::kBase;
  // A foreign event after the join, or catch staging of a span event, may
  // already keep the selection itself uncommitted; nothing is crossed then.
  test::Check(!evaluation.phase_selections.empty() || variant == Variant::kForeignAfterJoin ||
      variant == Variant::kCatchCopies);
  test::Check(evaluation.phase_compositions.empty() != composed);
  test::Check((evaluation.operations[callee->value].kind == EvaluationKind::kInline) == composed);
  test::Check(std::holds_alternative<CallPlan>(BuildPhasedCallPlan(graph, evaluation)) == composed);
  const auto checked = std::get<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, evaluation));
  test::Check(std::get<EvaluationError>(VerifyPhasedEvaluationPlan(
      graph, evaluation, {SemanticBudget{}.bytes, checked - 1})) == EvaluationError::kWorkLimit);
  test::Check(std::holds_alternative<EvaluationPlan>(BuildPhasedEvaluationPlan(
      graph, {SemanticBudget{}.bytes, evaluation.work}, IncomingCells::kReject)));
  test::Check(std::get<EvaluationError>(BuildPhasedEvaluationPlan(
      graph, {SemanticBudget{}.bytes, evaluation.work - 1}, IncomingCells::kReject)) == EvaluationError::kWorkLimit);
  const auto rejected = [&](const EvaluationPlan& forged) {
    test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
  };
  if (!composed) {
    // The builder's refusal is not a verifier gap: claiming the composition fails.
    auto forged = evaluation;
    forged.operations[callee->value] = {EvaluationKind::kInline, graph.ops[call->value].first_use, {}};
    rejected(forged);
    forged.phase_compositions.push_back(*call);
    rejected(forged);
    return evaluation.work;
  }
  test::Check(evaluation.phase_compositions.size() == 1 && evaluation.phase_compositions[0] == *call);
  for (const auto& selection : evaluation.phase_selections) test::Check(selection.execution == *call);
  auto forged = evaluation;
  forged.phase_compositions.clear();  // Inline callee without its composition proof.
  rejected(forged);
  forged = evaluation;
  forged.phase_compositions.push_back(*call);  // Duplicate root.
  rejected(forged);
  forged = evaluation;
  forged.phase_compositions.insert(forged.phase_compositions.begin(), *callee);  // Not a root.
  rejected(forged);
  forged = evaluation;
  forged.phase_selections[0].execution = *callee;  // Stale execution owner.
  rejected(forged);
  forged = evaluation;
  forged.operations[callee->value] = {EvaluationKind::kRoot, {}, {}};  // Snapshot claimed composed.
  rejected(forged);
  // Byte accounting is deterministic and bounded: find the smallest grant and
  // check that one byte less fails cleanly with a resource limit.
  std::size_t low = 0, high = SemanticBudget{}.bytes;
  while (low + 1 < high) {
    const auto middle = low + (high - low) / 2;
    if (std::holds_alternative<EvaluationPlan>(BuildPhasedEvaluationPlan(graph, {middle, evaluation.work},
        IncomingCells::kReject))) high = middle; else low = middle;
  }
  test::Check(std::get<EvaluationError>(BuildPhasedEvaluationPlan(graph, {high - 1, evaluation.work},
      IncomingCells::kReject)) == EvaluationError::kResourceLimit);
  return evaluation.work;
}
}  // namespace
int main() {
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                            abc::ProfileId::kDynamic1301})
    for (const auto variant : {Variant::kBase, Variant::kForeignBefore, Variant::kForeignAfterJoin,
                               Variant::kCallOutsideTry, Variant::kCatchCopies, Variant::kExtraUse})
      Run(profile, variant);
  // A failing chain must not be re-expanded per candidate: linear planner work.
  // A run of pure wrappers is not rescanned per consumer either.
  for (const auto variant : {Variant::kChain, Variant::kPureChain}) {
    std::uint64_t previous = 0;
    for (const auto count : {200U, 201U, 400U, 800U}) {
      const auto work = Run(abc::ProfileId::kDynamic1206, variant, count);
      if (count == 400 || count == 800) test::Check(work < 3 * previous);
      previous = work;
    }
  }
}
